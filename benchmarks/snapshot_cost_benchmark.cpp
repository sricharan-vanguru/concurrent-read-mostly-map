#include "read_mostly/read_mostly_map.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
// This separate single-thread executable counts ordinary allocation requests.
// It is never linked into the throughput benchmark or production library.
thread_local bool counting = false;
thread_local std::size_t allocations = 0, requested_bytes = 0;
void *allocate(std::size_t bytes) {
    if (void *result = std::malloc(bytes ? bytes : 1)) {
        if (counting) {
            ++allocations;
            requested_bytes += bytes;
        }
        return result;
    }
    throw std::bad_alloc();
}
std::size_t number(const char *argument) {
    const std::string_view text(argument);
    std::size_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0 ||
        value > 1000000) {
        throw std::invalid_argument("invalid profiling size");
    }
    return value;
}
} // namespace
void *operator new(std::size_t bytes) { return allocate(bytes); }
void *operator new[](std::size_t bytes) { return allocate(bytes); }
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { std::free(pointer); }

int main(int argc, char **argv) {
    using namespace read_mostly;
    using Clock = std::chrono::steady_clock;
    try {
        if (argc != 1 && argc != 4) {
            throw std::invalid_argument(
                "usage: snapshot_cost_benchmark [entries value_bytes repeats]");
        }
        const auto entries = argc == 4 ? number(argv[1]) : 256;
        const auto value_bytes = argc == 4 ? number(argv[2]) : 64;
        const auto repeats = argc == 4 ? number(argv[3]) : 31;
        Snapshot empty;
        SnapshotBuilder builder;
        UpdateTransaction seed, edit;
        std::vector<ReadMostlyMap::Entry> input;
        for (std::size_t index = 0; index < entries; ++index) {
            const auto key = "key-" + std::to_string(index);
            const std::string value(value_bytes, 'a');
            seed.insert_or_assign(key, value);
            input.emplace_back(key, value);
        }
        edit.insert_or_assign("key-0", std::string(value_bytes, 'b'));
        const auto source = builder.build(empty, seed);
        UpdateTransaction legacy_replacement;
        legacy_replacement.clear();
        for (const auto &[key, value] : input) {
            legacy_replacement.insert_or_assign(key, value);
        }
        std::cout << "# ordinary allocation requests only; instrumentation changes timing\n";
        std::cout << "iteration,entries,value_bytes,build_ns,allocations,requested_bytes,last_"
                     "owner_release_ns\n";
        for (std::size_t iteration = 0; iteration < repeats; ++iteration) {
            allocations = 0;
            requested_bytes = 0;
            counting = true;
            const auto start = Clock::now();
            auto candidate = builder.build(source, edit);
            const auto finish = Clock::now();
            counting = false;
            const auto release_start = Clock::now();
            candidate = empty; // Final owner destroys candidate table here.
            const auto release_finish = Clock::now();
            std::cout
                << iteration << ',' << entries << ',' << value_bytes << ','
                << std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count()
                << ',' << allocations << ',' << requested_bytes << ','
                << std::chrono::duration_cast<std::chrono::nanoseconds>(release_finish -
                                                                        release_start)
                       .count()
                << '\n';
        }
        // Compare candidate construction only. Input/transaction setup is outside
        // counting and timing, so the legacy path's extra preparation is excluded.
        std::cout << "# replacement candidate comparison: identical source and input\n";
        std::cout << "replacement_mode,iteration,build_ns,allocations,requested_bytes\n";
        for (std::size_t iteration = 0; iteration < repeats; ++iteration) {
            for (std::size_t order = 0; order < 2; ++order) {
                const bool direct = (iteration + order) % 2 == 0;
                allocations = 0;
                requested_bytes = 0;
                counting = true;
                const auto start = Clock::now();
                const auto candidate = direct ? builder.build_replacement(source, input)
                                              : builder.build(source, legacy_replacement);
                const auto finish = Clock::now();
                counting = false;
                if (candidate.size() != source.size() ||
                    candidate.payload_bytes() != source.payload_bytes() ||
                    candidate.version() != source.version() + 1 ||
                    candidate.find_copy("key-0") != source.find_copy("key-0")) {
                    throw std::runtime_error("replacement comparison invariant failed");
                }
                std::cout
                    << (direct ? "direct" : "copy_clear") << ',' << iteration << ','
                    << std::chrono::duration_cast<std::chrono::nanoseconds>(finish - start).count()
                    << ',' << allocations << ',' << requested_bytes << '\n';
            }
        }
        MapOptions options;
        options.track_retained_snapshots = true;
        ReadMostlyMap map({}, options);
        (void)map.replace_all(input);
        std::vector<Snapshot> held;
        held.reserve(8);
        for (int index = 0; index < 8; ++index) {
            held.push_back(map.acquire_snapshot());
            (void)map.commit(edit);
        }
        const auto retained = map.statistics();
        held.clear();
        const auto released = map.statistics();
        std::cout << "# retained_versions=" << retained.retired_snapshots
                  << ", retained_payload_bytes=" << retained.retired_payload_bytes
                  << ", after_release_versions=" << released.retired_snapshots << '\n';
        if (retained.retired_snapshots != 8 || released.retired_snapshots != 0) {
            throw std::runtime_error("retention experiment invariant failed");
        }
    } catch (const std::exception &error) {
        counting = false;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
