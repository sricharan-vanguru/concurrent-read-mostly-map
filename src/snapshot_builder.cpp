#include "read_mostly/snapshot_builder.hpp"
#include "snapshot_data.hpp"
#include "transaction_data.hpp"
#include "version.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace read_mostly {
namespace {
template <class Data> void validate(Data &replacement, SnapshotLimits limits) {
    if (replacement.entries.size() > limits.max_entries) {
        throw std::length_error("snapshot entry limit exceeded");
    }
    std::size_t bytes = 0;
    for (const auto &[key, value] : replacement.entries) {
        for (const auto length : {key.size(), value.size()}) {
            if (length > limits.max_payload_bytes - bytes) {
                throw std::length_error("snapshot payload limit exceeded");
            }
            bytes += length;
        }
    }
    replacement.payload_bytes = bytes;
}
} // namespace
SnapshotBuilder::SnapshotBuilder(SnapshotLimits limits) noexcept : limits_(limits) {}
Snapshot SnapshotBuilder::build(const Snapshot &source,
                                const UpdateTransaction &transaction) const {
    if (transaction.size() > limits_.max_operations) {
        throw std::length_error("transaction operation limit exceeded");
    }
    const auto candidate_version = detail::next_version(source.version());
    // This copy remains private until all operations and validation succeed.
    auto replacement = std::make_shared<Snapshot::Data>(*source.data_);
    if (transaction.impl_) {
        for (const auto &operation : transaction.impl_->operations) {
            switch (operation.kind) {
            case UpdateTransaction::Impl::Kind::assign:
                replacement->entries.insert_or_assign(operation.key, operation.value);
                break;
            case UpdateTransaction::Impl::Kind::erase:
                replacement->entries.erase(operation.key);
                break;
            case UpdateTransaction::Impl::Kind::clear:
                replacement->entries.clear();
                break;
            }
        }
    }
    validate(*replacement, limits_);
    replacement->version = candidate_version;
    return Snapshot(std::move(replacement));
}
Snapshot SnapshotBuilder::build_replacement(
    const Snapshot &source, std::span<const std::pair<std::string, std::string>> entries) const {
    if (entries.size() > limits_.max_operations) {
        throw std::length_error("replacement operation limit exceeded");
    }
    const auto candidate_version = detail::next_version(source.version());
    auto replacement = std::make_shared<Snapshot::Data>();
    // Duplicates count as operations, but not as unique entries.
    replacement->entries.reserve(std::min(entries.size(), limits_.max_entries));
    for (const auto &[key, value] : entries) {
        replacement->entries.insert_or_assign(key, value);
    }
    validate(*replacement, limits_);
    replacement->version = candidate_version;
    return Snapshot(std::move(replacement));
}
} // namespace read_mostly
