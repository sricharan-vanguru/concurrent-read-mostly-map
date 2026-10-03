#include "retention_registry.hpp"
#include <algorithm>
#include <limits>

namespace read_mostly {
RetentionRegistry::RetentionRegistry(MapOptions options)
    : options_(options),
      enabled_(options.track_retained_snapshots ||
               options.max_live_payload_bytes != std::numeric_limits<std::size_t>::max() ||
               options.max_live_snapshots != std::numeric_limits<std::size_t>::max()) {}
bool RetentionRegistry::enabled() const noexcept { return enabled_; }
void RetentionRegistry::sweep() {
    std::erase_if(records_, [](const Record &record) { return record.lifetime.expired(); });
}
bool RetentionRegistry::admit(std::weak_ptr<const void> lifetime, std::size_t payload,
                              std::uint64_t version) {
    if (!enabled_) {
        return true;
    }
    sweep();
    if (records_.size() >= options_.max_live_snapshots) {
        return false;
    }
    auto available = options_.max_live_payload_bytes;
    for (const auto &record : records_) {
        if (record.payload > available) {
            return false;
        }
        available -= record.payload;
    }
    if (payload > available) {
        return false;
    }
    // May throw, but happens before publication, preserving the current version.
    records_.push_back({std::move(lifetime), payload, version, std::chrono::steady_clock::now()});
    return true;
}
void RetentionRegistry::inspect(MapStatistics &statistics) {
    statistics.retention_tracking_enabled = enabled_;
    if (!enabled_) {
        return;
    }
    sweep();
    const auto now = std::chrono::steady_clock::now();
    for (const auto &record : records_) {
        ++statistics.live_snapshots;
        const auto maximum = std::numeric_limits<std::size_t>::max();
        statistics.live_payload_bytes +=
            std::min(record.payload, maximum - statistics.live_payload_bytes);
        if (record.version == statistics.current_version) {
            continue;
        }
        ++statistics.retired_snapshots;
        statistics.retired_payload_bytes +=
            std::min(record.payload, maximum - statistics.retired_payload_bytes);
        if (!statistics.oldest_retired_version ||
            record.version < *statistics.oldest_retired_version) {
            statistics.oldest_retired_version = record.version;
            statistics.oldest_retired_age_nanoseconds = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - record.published_at)
                    .count());
        }
    }
}
} // namespace read_mostly
