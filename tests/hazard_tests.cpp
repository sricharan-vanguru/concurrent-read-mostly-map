#include "read_mostly/read_mostly_map.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace read_mostly;
void check(bool value) {
    if (!value) {
        throw std::runtime_error("hazard safety assertion failed");
    }
}
MapOptions hazard_options() {
    MapOptions options;
    options.backend = PublicationBackend::experimental_hazard;
    options.track_retained_snapshots = true;
    return options;
}
void sc_order_model() {
    // Enumerate total SC orders for load A, hazard store H, validation V,
    // data use D, clear C, writer exchange W, and reclamation scan S.
    std::array<int, 7> order{0, 1, 2, 3, 4, 5, 6};
    std::size_t schedules = 0;
    do {
        std::array<int, 7> position{};
        for (int index = 0; index < 7; ++index) {
            position[static_cast<std::size_t>(order[static_cast<std::size_t>(index)])] = index;
        }
        if (!(position[0] < position[1] && position[1] < position[2] && position[2] < position[3] &&
              position[3] < position[4] && position[5] < position[6])) {
            continue;
        }
        ++schedules;
        const bool validated_old = position[2] < position[5];
        const bool still_using_at_scan = position[6] < position[4];
        if (validated_old && still_using_at_scan) {
            check(position[1] < position[6]); // Scan cannot miss the hazard.
        }
    } while (std::next_permutation(order.begin(), order.end()));
    check(schedules != 0);
}
int main() {
    try {
        sc_order_model();
        ReadMostlyMap map({}, hazard_options());
        UpdateTransaction one, two;
        one.insert_or_assign("a", "1");
        one.insert_or_assign("b", "1");
        two.insert_or_assign("a", "2");
        two.insert_or_assign("b", "2");
        check(map.commit(one).version == 1);
        auto reader = map.register_reader();
        auto nested = map.register_reader();
        std::vector<HazardGuard> guards;
        guards.push_back(reader.acquire());
        const auto owned = guards.back().copy_snapshot();
        bool nested_rejected = false;
        try {
            (void)reader.acquire();
        } catch (const std::logic_error &) {
            nested_rejected = true;
        }
        check(nested_rejected);
        check(map.commit(two).version == 2);
        guards.push_back(nested.acquire());
        check(map.commit(one).version == 3);
        check(map.statistics().hazard_retired_wrappers == 2);
        check(guards.front().find_copy("a") == "1" && guards.back().find_copy("a") == "2");
        guards.pop_back();
        check(map.statistics().hazard_retired_wrappers == 1);
        guards.pop_back();
        check(map.statistics().hazard_retired_wrappers == 0);
        check(owned.find_copy("a") == "1"); // Owning conversion survives wrapper reclamation.
        auto moved_reader = std::move(reader);
        bool moved_rejected = false;
        try {
            (void)reader.acquire();
        } catch (const std::logic_error &) {
            moved_rejected = true;
        }
        check(moved_rejected);
        {
            auto guard = moved_reader.acquire();
            auto moved_guard = std::move(guard);
            bool moved_guard_rejected = false;
            try {
                (void)guard.version();
            } catch (const std::logic_error &) {
                moved_guard_rejected = true;
            }
            check(moved_guard_rejected && moved_guard.version() == 3);
        }
        const auto after_map_destruction = [] {
            ReadMostlyMap temporary({}, hazard_options());
            UpdateTransaction initial;
            initial.insert_or_assign("alive", "yes");
            (void)temporary.commit(initial);
            return temporary.register_reader();
        }();
        check(after_map_destruction.acquire().find_copy("alive") == "yes");
        auto guard_after_reader = [&] {
            auto temporary_reader = map.register_reader();
            return temporary_reader.acquire();
        }();
        check(guard_after_reader.version() == 3);

        MapOptions budget = hazard_options();
        budget.max_live_payload_bytes = 4;
        ReadMostlyMap limited({}, budget);
        const std::vector<ReadMostlyMap::Entry> entry{{"k", "1"}};
        (void)limited.replace_all(entry);
        auto delayed = limited.register_reader();
        {
            const auto old_guard = delayed.acquire();
            check(limited.replace_all(entry).version == 2);
            check(limited.replace_all(entry).status == CommitStatus::memory_budget_exceeded);
            check(old_guard.version() == 1);
        }
        check(limited.replace_all(entry).version == 3);
        {
            const auto old_guard = delayed.acquire();
            check(limited.replace_all(entry).version == 4);
            limited.close();
            check(!limited.drain_retired_until(std::chrono::steady_clock::now()));
        }
        check(limited.drain_retired_until(std::chrono::steady_clock::now() +
                                          std::chrono::seconds(1)));

        ReadMostlyMap stress({}, hazard_options());
        (void)stress.commit(one);
        std::barrier start(5);
        std::atomic<bool> correct{true};
        std::vector<std::jthread> readers;
        // Register on the main thread, then transfer one reader to each worker.
        for (int index = 0; index < 4; ++index) {
            auto registered = stress.register_reader();
            readers.emplace_back([&, registration = std::move(registered)] {
                start.arrive_and_wait();
                for (int iteration = 0; iteration < 4000; ++iteration) {
                    const auto guard = registration.acquire();
                    if (guard.find_copy("a") != guard.find_copy("b") || guard.size() != 2) {
                        correct = false;
                    }
                }
            });
        }
        start.arrive_and_wait();
        for (int iteration = 0; iteration < 400; ++iteration) {
            (void)stress.commit(iteration % 2 ? one : two);
        }
        readers.clear();
        check(correct && stress.statistics().hazard_retired_wrappers == 0);
        // Same commits/conflicts/replacement behavior as owning implementation.
        ReadMostlyMap baseline;
        ReadMostlyMap advanced({}, hazard_options());
        for (int index = 0; index < 100; ++index) {
            const auto &batch = index % 2 ? one : two;
            check(baseline.commit(batch).version == advanced.commit(batch).version);
            const auto left = baseline.acquire_snapshot(), right = advanced.acquire_snapshot();
            check(left.find_copy("a") == right.find_copy("a") && left.size() == right.size());
            check(baseline.commit(batch, 0).status == advanced.commit(batch, 0).status);
        }
        ReadMostlyMap parallel({}, hazard_options());
        std::barrier writer_start(5);
        std::vector<std::jthread> producers;
        for (int writer = 0; writer < 4; ++writer) {
            auto registration = parallel.register_reader();
            producers.emplace_back([&, writer, local = std::move(registration)] {
                writer_start.arrive_and_wait();
                for (int index = 0; index < 25; ++index) {
                    const auto key = std::to_string(writer) + ":" + std::to_string(index);
                    UpdateTransaction insert;
                    insert.insert_or_assign(key, "kept");
                    if (parallel.commit(insert).status != CommitStatus::committed ||
                        local.acquire().find_copy(key) != "kept") {
                        correct = false;
                    }
                }
            });
        }
        writer_start.arrive_and_wait();
        producers.clear();
        check(correct && parallel.size() == 100 && parallel.version() == 100 &&
              parallel.statistics().hazard_retired_wrappers == 0);
        std::cout << "SC order model, guards, domain lifetime, budgets, and stress passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
