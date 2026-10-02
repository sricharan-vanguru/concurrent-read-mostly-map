#include "read_mostly/read_mostly_map.hpp"
#include "owning_publication.hpp"
#include <limits>
#include <mutex>
#include <stdexcept>

namespace read_mostly {
struct ReadMostlyMap::Impl {
    explicit Impl(SnapshotLimits configured_limits) : limits(configured_limits) {}
    OwningPublication publication;
    std::mutex writer_mutex;
    SnapshotLimits limits;
    bool closed = false; // Accessed only under writer_mutex.

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
        publication.publish(candidate);
        return {CommitStatus::committed, next_version};
    }
};

ReadMostlyMap::ReadMostlyMap(SnapshotLimits limits) : impl_(std::make_unique<Impl>(limits)) {}
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
    const std::lock_guard lock(impl_->writer_mutex);
    const auto current = impl_->publication.acquire();
    if (const auto rejected = impl_->rejection(current, expected_version)) {
        return *rejected;
    }
    return impl_->publish(current, transaction, impl_->limits);
}
CommitResult ReadMostlyMap::replace_all(std::span<const Entry> entries,
                                        std::optional<std::uint64_t> expected_version) {
    const std::lock_guard lock(impl_->writer_mutex);
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
    // The internal clear operation does not consume the caller's input budget.
    if (build_limits.max_operations != std::numeric_limits<std::size_t>::max()) {
        ++build_limits.max_operations;
    }
    return impl_->publish(current, replacement, build_limits);
}
void ReadMostlyMap::close() {
    const std::lock_guard lock(impl_->writer_mutex);
    impl_->closed = true;
}
} // namespace read_mostly
