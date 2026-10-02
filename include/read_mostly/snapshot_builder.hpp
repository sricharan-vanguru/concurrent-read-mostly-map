#pragma once

#include "read_mostly/snapshot.hpp"
#include "read_mostly/update_transaction.hpp"

#include <cstddef>
#include <limits>

namespace read_mostly {
struct SnapshotLimits {
    std::size_t max_entries = std::numeric_limits<std::size_t>::max();
    // Counts key + value characters, not hash-table/allocator overhead.
    std::size_t max_payload_bytes = std::numeric_limits<std::size_t>::max();
    std::size_t max_operations = std::numeric_limits<std::size_t>::max();
};

// Stateless builder: failures never alter the source or the transaction.
class SnapshotBuilder final {
  public:
    explicit SnapshotBuilder(SnapshotLimits limits = {}) noexcept;
    [[nodiscard]] Snapshot build(const Snapshot &source,
                                 const UpdateTransaction &transaction) const;

  private:
    SnapshotLimits limits_;
};
} // namespace read_mostly
