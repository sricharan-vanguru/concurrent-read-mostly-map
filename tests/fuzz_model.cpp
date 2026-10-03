#include "read_mostly/read_mostly_map.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <unordered_map>
#include <vector>

namespace {
void require(bool condition) {
    if (!condition)
        std::abort();
}
// Fixed-size instructions bound memory and execution time even for huge inputs.
// Byte-derived strings include NULs and non-ASCII characters.
void exercise(const std::uint8_t *data, std::size_t size) {
    if (size == 0)
        return;
    read_mostly::MapOptions options;
    options.backend = (data[0] & 1U) != 0 ? read_mostly::PublicationBackend::experimental_hazard
                                          : read_mostly::PublicationBackend::shared_ownership;
    read_mostly::ReadMostlyMap map({}, options);
    std::unordered_map<std::string, std::string> model;
    const auto bounded = std::min(size, std::size_t{2049});
    for (std::size_t offset = 1; offset + 3 < bounded; offset += 4) {
        const auto old = map.acquire_snapshot();
        const auto old_model = model;
        auto candidate = model;
        const std::string key(1, static_cast<char>(data[offset + 1] % 16));
        const std::string value{static_cast<char>(data[offset + 2]),
                                static_cast<char>(data[offset + 3])};
        const auto kind = data[offset] % 8;
        const bool conflict = kind == 5;
        read_mostly::CommitResult result{read_mostly::CommitStatus::closed, 0};
        if (kind == 4) {
            const std::vector<read_mostly::ReadMostlyMap::Entry> entries{{key, "first"},
                                                                         {key, value}};
            candidate = {{key, value}};
            result = map.replace_all(entries, old.version());
        } else if (kind == 6) {
            map.close();
            result = map.commit({});
        } else {
            read_mostly::UpdateTransaction batch;
            if (kind == 0) {
                batch.clear();
                candidate.clear();
            } else if (kind == 1) {
                batch.erase(key);
                candidate.erase(key);
            } else {
                batch.insert_or_assign(key, value);
                candidate.insert_or_assign(key, value);
                if (kind == 7) {
                    batch.erase(key);
                    candidate.erase(key);
                }
            }
            result = map.commit(batch, old.version() + (conflict ? 1U : 0U));
        }
        if (result.status == read_mostly::CommitStatus::committed)
            model = candidate;
        else
            require(result.status == read_mostly::CommitStatus::closed ||
                    (conflict && result.status == read_mostly::CommitStatus::version_conflict));
        const auto now = map.acquire_snapshot();
        require(now.size() == model.size() && old.size() == old_model.size());
        require(now.version() ==
                old.version() + (result.status == read_mostly::CommitStatus::committed ? 1U : 0U));
        require(result.version == now.version());
        for (unsigned index = 0; index < 16; ++index) {
            const std::string lookup(1, static_cast<char>(index));
            const auto current = model.find(lookup);
            const auto previous = old_model.find(lookup);
            require(now.find_copy(lookup) == (current == model.end()
                                                  ? std::nullopt
                                                  : std::optional<std::string>(current->second)));
            require(old.find_copy(lookup) == (previous == old_model.end()
                                                  ? std::nullopt
                                                  : std::optional<std::string>(previous->second)));
        }
    }
}
} // namespace
#ifdef READ_MOSTLY_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    exercise(data, size);
    return 0;
}
#else
int main(int argc, char **argv) {
    if (argc > 1) {
        for (int index = 1; index < argc; ++index) {
            std::ifstream input(argv[index], std::ios::binary);
            if (!input) {
                std::cerr << "Cannot open corpus input\n";
                return 1;
            }
            const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
            exercise(bytes.data(), bytes.size());
        }
    } else {
        std::mt19937 random(20261003U);
        for (int run = 0; run < 128; ++run) {
            std::vector<std::uint8_t> bytes(257);
            for (auto &byte : bytes)
                byte = static_cast<std::uint8_t>(random());
            exercise(bytes.data(), bytes.size());
        }
    }
    std::cout << "Fuzz model replay passed\n";
}
#endif
