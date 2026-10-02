#include "read_mostly/snapshot_builder.hpp"
#include "snapshot_data.hpp"
#include "transaction_data.hpp"
#include <stdexcept>
#include <utility>

namespace read_mostly {
SnapshotBuilder::SnapshotBuilder(SnapshotLimits limits) noexcept : limits_(limits) {}
Snapshot SnapshotBuilder::build(const Snapshot &source,
                                const UpdateTransaction &transaction) const {
    if (transaction.size() > limits_.max_operations) {
        throw std::length_error("transaction operation limit exceeded");
    }
    if (source.version() == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("snapshot version exhausted");
    }
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
    if (replacement->entries.size() > limits_.max_entries) {
        throw std::length_error("snapshot entry limit exceeded");
    }
    std::size_t bytes = 0;
    for (const auto &[key, value] : replacement->entries) {
        // Subtraction-based checks avoid overflow in the running byte count.
        for (const auto length : {key.size(), value.size()}) {
            if (length > limits_.max_payload_bytes - bytes) {
                throw std::length_error("snapshot payload limit exceeded");
            }
            bytes += length;
        }
    }
    replacement->payload_bytes = bytes;
    replacement->version = source.version() + 1;
    return Snapshot(std::move(replacement));
}
} // namespace read_mostly
