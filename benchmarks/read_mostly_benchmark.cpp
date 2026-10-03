#include "read_mostly/read_mostly_map.hpp"
#include <algorithm>
#include <barrier>
#include <charconv>
#include <chrono>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

using Clock = std::chrono::steady_clock;
struct Config {
    std::size_t threads = 4, entries = 1024, iterations = 10000;
    std::size_t key_bytes = 16, value_bytes = 32, batch = 1, write_permille = 0, repeats = 3;
    bool burst = false;
};
std::size_t number(std::string_view argument) {
    std::size_t value = 0;
    const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size()) {
        throw std::invalid_argument("invalid numeric option");
    }
    return value;
}
Config parse(int argc, char **argv) {
    Config result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (index + 1 == argc) {
            throw std::invalid_argument("option requires a value");
        }
        const auto value = number(argv[++index]);
        if (option == "--threads") {
            result.threads = value;
        } else if (option == "--entries") {
            result.entries = value;
        } else if (option == "--iterations") {
            result.iterations = value;
        } else if (option == "--value-bytes") {
            result.value_bytes = value;
        } else if (option == "--key-bytes") {
            result.key_bytes = value;
        } else if (option == "--batch") {
            result.batch = value;
        } else if (option == "--write-permille") {
            result.write_permille = value;
        } else if (option == "--repeats") {
            result.repeats = value;
        } else if (option == "--burst" && value <= 1) {
            result.burst = value != 0;
        } else {
            throw std::invalid_argument("unknown option");
        }
    }
    if (!result.threads || result.threads > 256 || !result.entries || !result.iterations ||
        !result.batch || !result.repeats || result.write_permille > 1000 ||
        result.iterations > 100000000 || result.batch > 1024 || result.repeats > 100) {
        throw std::invalid_argument("invalid workload bounds");
    }
    return result;
}
template <class Mutex> class LockedTable {
    Mutex mutex_;
    std::unordered_map<std::string, std::string> entries_;

  public:
    explicit LockedTable(const std::vector<read_mostly::ReadMostlyMap::Entry> &entries) {
        for (const auto &[key, value] : entries) {
            entries_.emplace(key, value);
        }
    }
    void write(const std::string &key, const std::string &value) {
        const std::lock_guard lock(mutex_);
        entries_.insert_or_assign(key, value);
    }
    std::uint64_t read(const std::vector<std::string> &keys, std::size_t start, std::size_t count) {
        const auto lookup = [&] {
            std::uint64_t checksum = 0;
            for (std::size_t index = 0; index < count; ++index) {
                const auto &value = entries_.at(keys[(start + index) % keys.size()]);
                checksum +=
                    value.size() + (value.empty() ? 0 : static_cast<unsigned char>(value[0]));
            }
            return checksum;
        };
        if constexpr (std::is_same_v<Mutex, std::shared_mutex>) {
            const std::shared_lock lock(mutex_);
            return lookup();
        } else {
            const std::lock_guard lock(mutex_);
            return lookup();
        }
    }
};
class CowTable {
    read_mostly::ReadMostlyMap map_;

  public:
    explicit CowTable(const std::vector<read_mostly::ReadMostlyMap::Entry> &entries) {
        (void)map_.replace_all(entries);
    }
    void write(const std::string &key, const std::string &value) {
        read_mostly::UpdateTransaction update;
        update.insert_or_assign(key, value);
        if (map_.commit(update).status != read_mostly::CommitStatus::committed) {
            throw std::runtime_error("benchmark update rejected");
        }
    }
    std::uint64_t read(const std::vector<std::string> &keys, std::size_t start, std::size_t count) {
        const auto snapshot = map_.acquire_snapshot();
        std::uint64_t checksum = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const auto *value = snapshot.find(keys[(start + index) % keys.size()]);
            if (!value) {
                throw std::runtime_error("missing benchmark key");
            }
            checksum +=
                value->size() + (value->empty() ? 0 : static_cast<unsigned char>((*value)[0]));
        }
        return checksum;
    }
};
std::uint64_t percentile(std::vector<std::uint64_t> &samples, std::size_t percent) {
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    return samples[((samples.size() - 1) * percent) / 100];
}
template <class Table>
void run(std::string_view name, const Config &config,
         const std::vector<read_mostly::ReadMostlyMap::Entry> &entries,
         const std::vector<std::string> &keys, std::size_t repeat) {
    Table table(entries);
    for (std::size_t index = 0; index < 256; ++index) {
        (void)table.read(keys, index % keys.size(), 1);
    }
    const std::string values[]{std::string(config.value_bytes, 'a'),
                               std::string(config.value_bytes, 'b')};
    struct Result {
        std::uint64_t checksum = 0, reads = 0, writes = 0;
        std::vector<std::uint64_t> read_ns, write_ns;
        std::exception_ptr error;
    };
    std::vector<Result> results(config.threads);
    for (auto &result : results) {
        result.read_ns.reserve(config.iterations / 64 + 1);
        result.write_ns.reserve(config.iterations);
    }
    std::barrier start(static_cast<std::ptrdiff_t>(config.threads + 1));
    std::vector<std::jthread> threads;
    threads.reserve(config.threads);
    try {
        for (std::size_t thread = 0; thread < config.threads; ++thread) {
            threads.emplace_back([&, thread] {
                auto &result = results[thread];
                // Input generation/allocation stays outside the measured interval.
                const auto &value = values[thread % 2];
                start.arrive_and_wait(); // All threads ready before starting timer.
                start.arrive_and_wait();
                try {
                    for (std::size_t iteration = 0; iteration < config.iterations; ++iteration) {
                        const bool write = config.burst
                                               ? iteration % 1000 < config.write_permille
                                               : (iteration % 1000 * config.write_permille) % 1000 <
                                                     config.write_permille;
                        const auto key = (iteration * 17 + thread * 31) % keys.size();
                        const bool timed = write || iteration % 64 == 0;
                        const auto before = timed ? Clock::now() : Clock::time_point{};
                        if (write) {
                            table.write(keys[key], value);
                            ++result.writes;
                        } else {
                            result.checksum += table.read(keys, key, config.batch);
                            result.reads += config.batch;
                        }
                        if (timed) {
                            const auto ns = static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                                                     before)
                                    .count());
                            (write ? result.write_ns : result.read_ns).push_back(ns);
                        }
                    }
                } catch (...) {
                    result.error = std::current_exception();
                }
            });
        }
    } catch (...) {
        // Drop missing participants and the main thread. Already started
        // workers can finish both barrier phases before their jthreads join.
        for (std::size_t missing = threads.size(); missing <= config.threads; ++missing) {
            start.arrive_and_drop();
        }
        threads.clear();
        throw;
    }
    start.arrive_and_wait();
    const auto before = Clock::now();
    start.arrive_and_wait();
    threads.clear();
    const double seconds = std::chrono::duration<double>(Clock::now() - before).count();
    std::uint64_t reads = 0, writes = 0, checksum = 0;
    std::vector<std::uint64_t> read_ns, write_ns;
    for (auto &result : results) {
        if (result.error) {
            std::rethrow_exception(result.error);
        }
        reads += result.reads;
        writes += result.writes;
        checksum += result.checksum;
        read_ns.insert(read_ns.end(), result.read_ns.begin(), result.read_ns.end());
        write_ns.insert(write_ns.end(), result.write_ns.begin(), result.write_ns.end());
    }
    std::cout << name << ',' << repeat << ',' << config.threads << ',' << config.entries << ','
              << config.key_bytes << ',' << config.value_bytes << ',' << config.batch << ','
              << config.write_permille << ',' << config.burst << ',' << reads << ',' << writes
              << ',' << seconds << ',' << static_cast<double>(reads + writes) / seconds << ','
              << percentile(read_ns, 50) << ',' << percentile(read_ns, 95) << ','
              << percentile(read_ns, 99) << ',' << percentile(write_ns, 50) << ','
              << percentile(write_ns, 95) << ',' << percentile(write_ns, 99) << ','
              << read_ns.size() << ',' << write_ns.size() << ',' << checksum << '\n';
}
int main(int argc, char **argv) {
    try {
        const auto config = parse(argc, argv);
        std::vector<read_mostly::ReadMostlyMap::Entry> entries;
        std::vector<std::string> keys;
        for (std::size_t index = 0; index < config.entries; ++index) {
            auto key = "key-" + std::to_string(index);
            key.resize(std::max(key.size(), config.key_bytes), 'k');
            keys.push_back(std::move(key));
            entries.emplace_back(keys.back(), std::string(config.value_bytes, 'x'));
        }
        std::cout << "# compiler="
#ifdef __VERSION__
                  << __VERSION__
#else
                  << "unknown"
#endif
                  << ", hardware_threads=" << std::thread::hardware_concurrency()
                  << ", clock=steady, read_sample_stride=64\n";
        std::cout << "mode,repeat,threads,entries,key_bytes,value_bytes,batch,write_permille,burst,"
                     "reads,writes,"
                     "seconds,ops_per_second,read_p50_ns,read_p95_ns,read_p99_ns,"
                     "write_p50_ns,write_p95_ns,write_p99_ns,read_samples,write_samples,checksum\n";
        for (std::size_t repeat = 0; repeat < config.repeats; ++repeat) {
            // Rotate order to reduce systematic first-mode bias.
            for (std::size_t mode = 0; mode < 3; ++mode) {
                switch ((mode + repeat) % 3) {
                case 0:
                    run<LockedTable<std::mutex>>("mutex", config, entries, keys, repeat);
                    break;
                case 1:
                    run<LockedTable<std::shared_mutex>>("shared_mutex", config, entries, keys,
                                                        repeat);
                    break;
                default:
                    run<CowTable>("cow", config, entries, keys, repeat);
                    break;
                }
            }
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
