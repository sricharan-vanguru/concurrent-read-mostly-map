#pragma once
#include "read_mostly/hazard_reader.hpp"
#include "retention_registry.hpp"
#include <atomic>
#include <mutex>
#include <vector>

namespace read_mostly::detail {
class HazardDomain;
struct HazardSlot {
    explicit HazardSlot(std::shared_ptr<HazardDomain> owner);
    std::shared_ptr<HazardDomain> domain;
    std::atomic<const Snapshot *> protected_snapshot{nullptr};
    std::atomic<bool> in_use{false};
};
class HazardDomain final : public std::enable_shared_from_this<HazardDomain> {
  public:
    explicit HazardDomain(RetentionRegistry &registry);
    HazardReader register_reader();
    const Snapshot *protect(HazardSlot &slot) const noexcept;
    // These operations require the façade's writer mutex.
    Snapshot current_snapshot() const;
    bool publish(Snapshot snapshot, RetentionRegistry &registry);
    void collect();
    std::size_t retired_count() const noexcept;

  private:
    std::unique_ptr<const Snapshot> owner_;
    std::atomic<const Snapshot *> current_;
    std::vector<std::unique_ptr<const Snapshot>> retired_;
    std::mutex slots_mutex_;
    std::vector<std::weak_ptr<HazardSlot>> slots_;
};
} // namespace read_mostly::detail
