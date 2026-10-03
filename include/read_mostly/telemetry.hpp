#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace read_mostly {
enum class UpdateOutcome { committed, version_conflict, closed, memory_budget_exceeded, exception };
struct UpdateEvent {
    UpdateOutcome outcome;
    std::uint64_t version;
    std::uint64_t elapsed_nanoseconds;
};
using UpdateObserver = void (*)(const UpdateEvent &, void *) noexcept;
enum class PublicationBackend { shared_ownership, experimental_hazard };

struct MapOptions {
    // Includes old current + candidate overlap, not allocator overhead.
    std::size_t max_live_payload_bytes = std::numeric_limits<std::size_t>::max();
    std::size_t max_live_snapshots = std::numeric_limits<std::size_t>::max();
    bool track_retained_snapshots = false;
    bool collect_update_metrics = false;
    UpdateObserver observer = nullptr;
    void *observer_context = nullptr;
    PublicationBackend backend = PublicationBackend::shared_ownership;
};
struct MapStatistics {
    bool hazard_backend = false;
    std::size_t hazard_retired_wrappers = 0;
    bool retention_tracking_enabled = false;
    bool update_metrics_enabled = false;
    bool closed = false;
    std::uint64_t current_version = 0;
    std::size_t current_payload_bytes = 0;
    // These gauges are valid only when retention_tracking_enabled is true.
    std::size_t live_snapshots = 0;
    std::size_t live_payload_bytes = 0;
    std::size_t retired_snapshots = 0;
    std::size_t retired_payload_bytes = 0;
    std::optional<std::uint64_t> oldest_retired_version;
    std::uint64_t oldest_retired_age_nanoseconds = 0;
    // Saturating counters; no per-read instrumentation.
    std::uint64_t attempts = 0;
    std::uint64_t commits = 0;
    std::uint64_t conflicts = 0;
    std::uint64_t closed_rejections = 0;
    std::uint64_t budget_rejections = 0;
    std::uint64_t exceptions = 0;
    std::uint64_t total_update_nanoseconds = 0;
    std::uint64_t max_update_nanoseconds = 0;
};
} // namespace read_mostly
