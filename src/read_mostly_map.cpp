#include "read_mostly/read_mostly_map.hpp"
#include "hazard_domain.hpp"
#include "owning_publication.hpp"
#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace read_mostly {
namespace {
bool acquire_until(std::unique_lock<std::mutex> &lock,
                   std::chrono::steady_clock::time_point deadline) {
    while (!lock.try_lock()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        std::this_thread::sleep_until(std::min(deadline, now + std::chrono::milliseconds(1)));
    }
    return true;
}
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
        : options(configured_options), registry(options), limits(configured_limits) {
        if (options.backend == PublicationBackend::experimental_hazard) {
            hazard = std::make_shared<detail::HazardDomain>(registry);
        } else {
            publication = std::make_unique<OwningPublication>(registry);
        }
    }
    MapOptions options;
    RetentionRegistry registry;
    std::unique_ptr<OwningPublication> publication;
    std::shared_ptr<detail::HazardDomain> hazard;
    mutable std::mutex writer_mutex;
    SnapshotLimits limits;
    bool closed = false;
    MapStatistics counters;

    // Writer mutex protects the raw-pointer backend's owner and retire list.
    Snapshot current_snapshot() const {
        return hazard ? hazard->current_snapshot() : publication->acquire();
    }

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
    CommitResult publish(const Snapshot &source, const Snapshot &candidate) {
        const auto next_version = candidate.version();
        const bool accepted = hazard ? hazard->publish(candidate, registry)
                                     : publication->publish(candidate, registry);
        if (!accepted) {
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
            const auto event = event_for(UpdateOutcome::exception, current_snapshot().version());
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
        if (hazard) {
            hazard->collect();
        }
        const auto current = current_snapshot();
        result.hazard_backend = static_cast<bool>(hazard);
        result.hazard_retired_wrappers = hazard ? hazard->retired_count() : 0;
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
Snapshot ReadMostlyMap::acquire_snapshot() const {
    if (impl_->hazard) {
        // Compatibility API: ephemeral registration plus owned data copy.
        // For repeated reads use a pre-registered reader and guard instead.
        return impl_->hazard->register_reader().acquire().copy_snapshot();
    }
    return impl_->publication->acquire();
}
HazardReader ReadMostlyMap::register_reader() const {
    if (!impl_->hazard) {
        throw std::logic_error("map does not use the hazard backend");
    }
    return impl_->hazard->register_reader();
}
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
        const auto current = impl_->current_snapshot();
        if (const auto rejected = impl_->rejection(current, expected_version)) {
            return *rejected;
        }
        return impl_->publish(current, SnapshotBuilder(impl_->limits).build(current, transaction));
    });
}
CommitResult ReadMostlyMap::replace_all(std::span<const Entry> entries,
                                        std::optional<std::uint64_t> expected_version) {
    return impl_->update([&] {
        const auto current = impl_->current_snapshot();
        if (const auto rejected = impl_->rejection(current, expected_version)) {
            return *rejected;
        }
        return impl_->publish(current,
                              SnapshotBuilder(impl_->limits).build_replacement(current, entries));
    });
}
void ReadMostlyMap::close() {
    const std::lock_guard lock(impl_->writer_mutex);
    impl_->closed = true;
}
bool ReadMostlyMap::close_until(std::chrono::steady_clock::time_point deadline) {
    std::unique_lock lock(impl_->writer_mutex, std::defer_lock);
    if (!acquire_until(lock, deadline)) {
        return false;
    }
    impl_->closed = true;
    return true;
}
bool ReadMostlyMap::drain_retired_until(std::chrono::steady_clock::time_point deadline) {
    for (;;) {
        {
            std::unique_lock lock(impl_->writer_mutex, std::defer_lock);
            if (!acquire_until(lock, deadline)) {
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
