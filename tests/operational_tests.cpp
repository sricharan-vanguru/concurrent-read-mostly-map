#include "read_mostly/read_mostly_map.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace read_mostly;
using Clock = std::chrono::steady_clock;
void check(bool condition) {
    if (!condition) {
        throw std::runtime_error("operational contract assertion failed");
    }
}
struct ObserverState {
    ReadMostlyMap *map = nullptr;
    std::atomic<std::uint64_t> events{0};
    std::atomic<bool> correct{true};
};
void observe(const UpdateEvent &event, void *context) noexcept {
    auto &state = *static_cast<ObserverState *>(context);
    try {
        // Requires callback to run outside the writer mutex.
        const auto statistics = state.map->statistics();
        if (statistics.current_version < event.version) {
            state.correct = false;
        }
        ++state.events;
    } catch (...) {
        state.correct = false;
    }
}
int main() {
    try {
        ObserverState observed;
        MapOptions options;
        options.max_live_payload_bytes = 4;
        options.collect_update_metrics = true;
        options.observer = observe;
        options.observer_context = &observed;
        ReadMostlyMap map({}, options);
        observed.map = &map;
        const std::vector<ReadMostlyMap::Entry> entries{{"a", "1"}};
        check(map.replace_all(entries).version == 1);
        Snapshot held = map.acquire_snapshot();
        Snapshot copied = held;
        check(map.replace_all(entries).version == 2);
        const auto budget = map.replace_all(entries);
        check(budget.status == CommitStatus::memory_budget_exceeded && budget.version == 2);
        held = Snapshot{};
        check(map.replace_all(entries).status == CommitStatus::memory_budget_exceeded);
        auto statistics = map.statistics();
        check(statistics.retention_tracking_enabled && statistics.live_snapshots == 2 &&
              statistics.live_payload_bytes == 4 && statistics.retired_payload_bytes == 2 &&
              statistics.oldest_retired_version == 1 && statistics.retired_snapshots == 1);
        copied = Snapshot{};
        check(map.replace_all(entries).version == 3);
        check(map.statistics().retired_snapshots == 0);
        const auto conflict = map.replace_all(entries, 0);
        check(conflict.status == CommitStatus::version_conflict);
        const auto before_reads = map.statistics().attempts;
        for (int index = 0; index < 100; ++index) {
            check(map.find_copy("a") == "1");
        }
        check(map.statistics().attempts == before_reads);
        check(map.close_until(Clock::now() + std::chrono::seconds(1)));
        check(map.commit(UpdateTransaction{}).status == CommitStatus::closed);
        statistics = map.statistics();
        check(statistics.attempts == 7 && statistics.commits == 3 &&
              statistics.budget_rejections == 2 && statistics.conflicts == 1 &&
              statistics.closed_rejections == 1 && statistics.closed);
        check(statistics.max_update_nanoseconds <= statistics.total_update_nanoseconds);
        check(observed.events == 7 && observed.correct);
        check(map.drain_retired_until(Clock::now() + std::chrono::seconds(1)));
        check(map.find_copy("a") == "1");

        // A zero-payload table still consumes a snapshot slot.
        MapOptions slots;
        slots.max_live_snapshots = 2;
        ReadMostlyMap zero_payload({}, slots);
        Snapshot initial = zero_payload.acquire_snapshot();
        check(zero_payload.commit(UpdateTransaction{}).version == 1);
        check(zero_payload.commit(UpdateTransaction{}).status ==
              CommitStatus::memory_budget_exceeded);
        check(zero_payload.statistics().live_payload_bytes == 0);
        zero_payload.close();
        check(!zero_payload.drain_retired_until(Clock::now()));
        initial = Snapshot{};
        check(zero_payload.drain_retired_until(Clock::now() + std::chrono::seconds(1)));

        ReadMostlyMap untracked;
        check(!untracked.statistics().retention_tracking_enabled &&
              !untracked.statistics().update_metrics_enabled);
        untracked.close();
        bool invalid_drain = false;
        try {
            (void)untracked.drain_retired_until(Clock::now() + std::chrono::seconds(1));
        } catch (const std::logic_error &) {
            invalid_drain = true;
        }
        check(invalid_drain);
        bool invalid_budget = false;
        try {
            MapOptions impossible;
            impossible.max_live_snapshots = 0;
            ReadMostlyMap invalid({}, impossible);
        } catch (const std::invalid_argument &) {
            invalid_budget = true;
        }
        check(invalid_budget);

        ObserverState parallel_observer;
        MapOptions telemetry;
        telemetry.collect_update_metrics = true;
        telemetry.track_retained_snapshots = true;
        telemetry.observer = observe;
        telemetry.observer_context = &parallel_observer;
        ReadMostlyMap concurrent({}, telemetry);
        parallel_observer.map = &concurrent;
        std::vector<std::jthread> writers;
        for (int writer = 0; writer < 4; ++writer) {
            writers.emplace_back([&, writer] {
                for (int index = 0; index < 25; ++index) {
                    UpdateTransaction batch;
                    batch.insert_or_assign(std::to_string(writer), std::to_string(index));
                    (void)concurrent.commit(batch);
                }
            });
        }
        writers.clear();
        check(concurrent.statistics().commits == 100 && parallel_observer.events == 100 &&
              parallel_observer.correct && concurrent.statistics().retired_snapshots == 0);
        // Validation errors also produce metrics and observer events.
        MapOptions failure_metrics;
        failure_metrics.collect_update_metrics = true;
        ObserverState failures;
        failure_metrics.observer = observe;
        failure_metrics.observer_context = &failures;
        ReadMostlyMap limited({0, 0, 0}, failure_metrics);
        failures.map = &limited;
        bool failed = false;
        try {
            (void)limited.replace_all(entries);
        } catch (const std::length_error &) {
            failed = true;
        }
        check(failed && limited.statistics().exceptions == 1 && limited.version() == 0 &&
              failures.events == 1 && failures.correct);
        std::cout << "Budgets, retained ownership, metrics, observers, and drain tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
