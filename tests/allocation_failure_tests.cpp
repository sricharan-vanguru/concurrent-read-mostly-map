#include "read_mostly/read_mostly_map.hpp"
#include <barrier>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
// Isolated executable only. Fail the Nth ordinary allocation on this thread,
// including STL storage and the publication wrapper, without production hooks.
thread_local std::size_t remaining = std::numeric_limits<std::size_t>::max();
struct AllocationGate {
    std::barrier<> entered{2};
    std::barrier<> release{2};
};
thread_local AllocationGate *allocation_gate = nullptr;
void *allocate(std::size_t bytes) {
    if (allocation_gate) {
        auto *gate = allocation_gate;
        allocation_gate = nullptr;
        gate->entered.arrive_and_wait();
        gate->release.arrive_and_wait();
    }
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
        for (const auto backend :
             {PublicationBackend::shared_ownership, PublicationBackend::experimental_hazard}) {
            for (const bool tracked : {false, true}) {
                for (const bool replace : {false, true}) {
                    bool reached_success = false;
                    std::size_t failures = 0;
                    for (std::size_t allocation = 0; allocation < 512; ++allocation) {
                        MapOptions options;
                        options.backend = backend;
                        options.track_retained_snapshots = tracked;
                        options.collect_update_metrics = tracked;
                        ReadMostlyMap map({}, options);
                        // Keep version 0 alive so the next candidate grows the weak
                        // registry, exercising its allocation-failure guarantee too.
                        const Snapshot initial_owner = map.acquire_snapshot();
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
                        check(initial_owner.version() == 0 && initial_owner.size() == 0);
                        if (!failed) {
                            check(map.version() == 2 && map.find_copy(key) == value);
                            reached_success = true;
                            break;
                        }
                        ++failures;
                        if (tracked) {
                            check(map.statistics().exceptions == 1 &&
                                  map.statistics().commits == 1 && map.statistics().attempts == 2);
                        }
                        check(map.version() == 1 && map.size() == 1 &&
                              map.find_copy("original") == value && !map.contains(key));
                        // Also proves the writer mutex is released after an exception.
                        const auto recovered =
                            replace ? map.replace_all(replacement) : map.commit(batch);
                        check(recovered.version == 2 && map.find_copy(key) == value);
                    }
                    check(reached_success && failures > 0);
                    std::cout << (backend == PublicationBackend::experimental_hazard ? "hazard "
                                                                                     : "")
                              << (tracked ? "tracked " : "") << (replace ? "replace_all" : "commit")
                              << ": exercised " << failures << " failing allocation positions\n";
                }
            }
        }
        MapOptions fast_options;
        fast_options.backend = PublicationBackend::experimental_hazard;
        ReadMostlyMap fast({}, fast_options);
        (void)fast.commit(initial);
        auto registered = fast.register_reader();
        {
            FailAfter no_allocation(0);
            const auto guard = registered.acquire();
            check(guard.find("original")->size() == value.size());
        }
        for (std::size_t fail_at = 0; fail_at < 2; ++fail_at) {
            ReadMostlyMap registration_target({}, fast_options);
            bool registration_failed = false;
            {
                FailAfter injection(fail_at);
                try {
                    (void)registration_target.register_reader();
                } catch (const std::bad_alloc &) {
                    registration_failed = true;
                }
            }
            check(registration_failed);
            check(registration_target.register_reader().acquire().version() == 0);
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
        // Pause a writer's first candidate allocation while its mutex is held.
        // Reads still progress; close_until times out without changing state.
        ReadMostlyMap contended;
        AllocationGate gate;
        std::jthread writer([&] {
            allocation_gate = &gate;
            (void)contended.commit(batch);
        });
        gate.entered.arrive_and_wait();
        const bool timed_out =
            !contended.close_until(std::chrono::steady_clock::now() + std::chrono::milliseconds(5));
        const bool unchanged = contended.version() == 0 && contended.size() == 0;
        gate.release.arrive_and_wait();
        writer.join();
        check(timed_out && unchanged && contended.version() == 1);
        check(contended.close_until(std::chrono::steady_clock::now() + std::chrono::seconds(1)));
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
