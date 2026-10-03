#include "read_mostly/read_mostly_map.hpp"
#include "owning_publication.hpp"
#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace read_mostly {
namespace {
void add_saturated(std::uint64_t &counter, std::uint64_t amount = 1) noexcept {
    counter += std::min(amount, std::numeric_limits<std::uint64_t>::max() - counter);
}
UpdateOutcome outcome(CommitStatus status) noexcept {
    switch (status) {
    case CommitStatus::committed:
        return UpdateOutcome::committed;
    case CommitStatus::version_conflict:
        return UpdateOutcome::version_conflict;
    case CommitStatus::closed:
        return UpdateOutcome::closed;
    case CommitStatus::memory_budget_exceeded:
        return UpdateOutcome::memory_budget_exceeded;
    }
    return UpdateOutcome::exception;
}
} // namespace
struct ReadMostlyMap::Impl {
    Impl(SnapshotLimits configured_limits, MapOptions configured_options)
        : options(configured_options), registry(options), publication(registry),
          limits(configured_limits) {}
    MapOptions options;
    RetentionRegistry registry;
    OwningPublication publication;
    mutable std::timed_mutex writer_mutex;
    SnapshotLimits limits;
    bool closed = false;
    MapStatistics counters;

    std::optional<CommitResult> rejection(const Snapshot &current,
                                          std::optional<std::uint64_t> expected) const {
        if (closed) {
            return CommitResult{CommitStatus::closed, current.version()};
        }
        if (expected && *expected != current.version()) {
            return CommitResult{CommitStatus::version_conflict, current.version()};
        }
        return std::nullopt;
    }
    CommitResult publish(const Snapshot &source, const UpdateTransaction &transaction,
                         SnapshotLimits build_limits) {
        auto candidate = SnapshotBuilder(build_limits).build(source, transaction);
        const auto next_version = candidate.version();
        if (!publication.publish(candidate, registry)) {
            return {CommitStatus::memory_budget_exceeded, source.version()};
        }
        return {CommitStatus::committed, next_version};
    }
    void record(UpdateEvent event) noexcept {
        if (!options.collect_update_metrics) {
            return;
        }
        switch (event.outcome) {
        case UpdateOutcome::committed:
            add_saturated(counters.commits);
            break;
        case UpdateOutcome::version_conflict:
            add_saturated(counters.conflicts);
            break;
        case UpdateOutcome::closed:
            add_saturated(counters.closed_rejections);
            break;
        case UpdateOutcome::memory_budget_exceeded:
            add_saturated(counters.budget_rejections);
            break;
        case UpdateOutcome::exception:
            add_saturated(counters.exceptions);
            break;
        }
        add_saturated(counters.total_update_nanoseconds, event.elapsed_nanoseconds);
        counters.max_update_nanoseconds =
            std::max(counters.max_update_nanoseconds, event.elapsed_nanoseconds);
    }
    template <class Function> CommitResult update(Function function) {
        const bool timed = options.collect_update_metrics || options.observer;
        const auto start =
            timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        std::unique_lock lock(writer_mutex);
        if (options.collect_update_metrics) {
            add_saturated(counters.attempts);
        }
        auto event_for = [&](UpdateOutcome result, std::uint64_t version) {
            return UpdateEvent{result, version,
                               timed ? static_cast<std::uint64_t>(
                                           std::chrono::duration_cast<std::chrono::nanoseconds>(
                                               std::chrono::steady_clock::now() - start)
                                               .count())
                                     : 0};
        };
        try {
            const auto result = function();
            const auto event = event_for(outcome(result.status), result.version);
            record(event);
            lock.unlock();
            if (options.observer) {
                options.observer(event, options.observer_context);
            }
            return result;
        } catch (...) {
            const auto event = event_for(UpdateOutcome::exception, publication.acquire().version());
            record(event);
            lock.unlock();
            if (options.observer) {
                options.observer(event, options.observer_context);
            }
            throw;
        }
    }
    MapStatistics inspect() {
        auto result = counters;
        const auto current = publication.acquire();
        result.update_metrics_enabled = options.collect_update_metrics;
        result.closed = closed;
        result.current_version = current.version();
        result.current_payload_bytes = current.payload_bytes();
        registry.inspect(result);
        return result;
    }
};

ReadMostlyMap::ReadMostlyMap(SnapshotLimits limits, MapOptions options)
    : impl_(std::make_unique<Impl>(limits, options)) {}
ReadMostlyMap::~ReadMostlyMap() = default;
Snapshot ReadMostlyMap::acquire_snapshot() const { return impl_->publication.acquire(); }
std::optional<std::string> ReadMostlyMap::find_copy(std::string_view key) const {
    return acquire_snapshot().find_copy(key);
}
bool ReadMostlyMap::contains(std::string_view key) const {
    return acquire_snapshot().contains(key);
}
std::size_t ReadMostlyMap::size() const { return acquire_snapshot().size(); }
std::uint64_t ReadMostlyMap::version() const { return acquire_snapshot().version(); }
CommitResult ReadMostlyMap::commit(const UpdateTransaction &transaction,
                                   std::optional<std::uint64_t> expected_version) {
    return impl_->update([&] {
        const auto current = impl_->publication.acquire();
        if (const auto rejected = impl_->rejection(current, expected_version)) {
            return *rejected;
        }
        return impl_->publish(current, transaction, impl_->limits);
    });
}
CommitResult ReadMostlyMap::replace_all(std::span<const Entry> entries,
                                        std::optional<std::uint64_t> expected_version) {
    return impl_->update([&] {
        const auto current = impl_->publication.acquire();
        if (const auto rejected = impl_->rejection(current, expected_version)) {
            return *rejected;
        }
        if (entries.size() > impl_->limits.max_operations) {
            throw std::length_error("replacement operation limit exceeded");
        }
        UpdateTransaction replacement;
        replacement.clear();
        for (const auto &[key, value] : entries) {
            replacement.insert_or_assign(key, value);
        }
        auto build_limits = impl_->limits;
        if (build_limits.max_operations != std::numeric_limits<std::size_t>::max()) {
            ++build_limits.max_operations;
        }
        return impl_->publish(current, replacement, build_limits);
    });
}
void ReadMostlyMap::close() {
    const std::lock_guard lock(impl_->writer_mutex);
    impl_->closed = true;
}
bool ReadMostlyMap::close_until(std::chrono::steady_clock::time_point deadline) {
    std::unique_lock lock(impl_->writer_mutex, std::defer_lock);
    if (!lock.try_lock_until(deadline)) {
        return false;
    }
    impl_->closed = true;
    return true;
}
bool ReadMostlyMap::drain_retired_until(std::chrono::steady_clock::time_point deadline) {
    for (;;) {
        {
            std::unique_lock lock(impl_->writer_mutex, std::defer_lock);
            if (!lock.try_lock_until(deadline)) {
                return false;
            }
            if (!impl_->closed || !impl_->registry.enabled()) {
                throw std::logic_error("drain requires a closed map and retention tracking");
            }
            if (impl_->inspect().retired_snapshots == 0) {
                return true;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        // Weak ownership expiry has no notification callback; poll on the
        // management thread only, never on the normal read/write path.
        std::this_thread::sleep_until(std::min(deadline, now + std::chrono::milliseconds(1)));
    }
}
MapStatistics ReadMostlyMap::statistics() const {
    const std::lock_guard lock(impl_->writer_mutex);
    return impl_->inspect();
}
} // namespace read_mostly
