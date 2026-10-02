#include "read_mostly/read_mostly_map.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
// Isolated executable only. Fail the Nth ordinary allocation on this thread,
// including STL storage and the publication wrapper, without production hooks.
thread_local std::size_t remaining = std::numeric_limits<std::size_t>::max();
void *allocate(std::size_t bytes) {
    if (remaining != std::numeric_limits<std::size_t>::max()) {
        if (remaining == 0) {
            throw std::bad_alloc();
        }
        --remaining;
    }
    if (void *result = std::malloc(bytes == 0 ? 1 : bytes)) {
        return result;
    }
    throw std::bad_alloc();
}
struct FailAfter {
    explicit FailAfter(std::size_t successful_allocations) { remaining = successful_allocations; }
    ~FailAfter() { remaining = std::numeric_limits<std::size_t>::max(); }
};
void check(bool condition) {
    if (!condition) {
        throw std::runtime_error("allocation failure guarantee violated");
    }
}
} // namespace
void *operator new(std::size_t bytes) { return allocate(bytes); }
void *operator new[](std::size_t bytes) { return allocate(bytes); }
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { std::free(pointer); }

int main() {
    using namespace read_mostly;
    try {
        const std::string key(128, 'k'), value(256, 'v');
        UpdateTransaction initial;
        initial.insert_or_assign("original", value);
        UpdateTransaction batch;
        batch.erase("original");
        batch.insert_or_assign(key, value);
        const std::vector<ReadMostlyMap::Entry> replacement{{key, value}};
        for (const bool replace : {false, true}) {
            bool reached_success = false;
            std::size_t failures = 0;
            for (std::size_t allocation = 0; allocation < 512; ++allocation) {
                ReadMostlyMap map;
                check(map.commit(initial).version == 1);
                const auto retained = map.acquire_snapshot();
                bool failed = false;
                {
                    FailAfter injection(allocation);
                    try {
                        if (replace) {
                            (void)map.replace_all(replacement);
                        } else {
                            (void)map.commit(batch);
                        }
                    } catch (const std::bad_alloc &) {
                        failed = true;
                    }
                }
                check(retained.version() == 1 && retained.find_copy("original") == value);
                if (!failed) {
                    check(map.version() == 2 && map.find_copy(key) == value);
                    reached_success = true;
                    break;
                }
                ++failures;
                check(map.version() == 1 && map.size() == 1 && map.find_copy("original") == value &&
                      !map.contains(key));
                // Also proves the writer mutex is released after an exception.
                const auto recovered = replace ? map.replace_all(replacement) : map.commit(batch);
                check(recovered.version == 2 && map.find_copy(key) == value);
            }
            check(reached_success && failures > 0);
            std::cout << (replace ? "replace_all" : "commit") << ": exercised " << failures
                      << " failing allocation positions\n";
        }
        // Closed/conflicting writes must not allocate a replacement at all.
        ReadMostlyMap rejected;
        {
            FailAfter injection(0);
            check(rejected.commit(batch, 1).status == CommitStatus::version_conflict);
        }
        rejected.close();
        {
            FailAfter injection(0);
            check(rejected.replace_all(replacement).status == CommitStatus::closed);
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
