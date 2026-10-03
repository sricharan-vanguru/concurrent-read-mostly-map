#pragma once
#include "read_mostly/snapshot.hpp"
#include <memory>

namespace read_mostly {
namespace detail {
class HazardDomain;
struct HazardSlot;
} // namespace detail
// A move-only guard. Borrowed values are valid only while this guard lives.
class HazardGuard final {
  public:
    ~HazardGuard();
    HazardGuard(HazardGuard &&) noexcept;
    HazardGuard &operator=(HazardGuard &&) noexcept;
    HazardGuard(const HazardGuard &) = delete;
    HazardGuard &operator=(const HazardGuard &) = delete;
    [[nodiscard]] const std::string *find(std::string_view key) const;
    [[nodiscard]] std::optional<std::string> find_copy(std::string_view key) const;
    [[nodiscard]] Snapshot copy_snapshot() const;
    [[nodiscard]] std::uint64_t version() const;
    [[nodiscard]] std::size_t size() const;

  private:
    HazardGuard(std::shared_ptr<detail::HazardSlot> slot, const Snapshot *snapshot) noexcept;
    std::shared_ptr<detail::HazardSlot> slot_;
    const Snapshot *snapshot_ = nullptr;
    void release() noexcept;
    const Snapshot &view() const;
    friend class HazardReader;
};
// Register once, reuse for reads. One guard at a time per reader; use a second
// registration for nested guards. Reader/guard ownership retains the domain.
class HazardReader final {
  public:
    HazardReader(HazardReader &&) noexcept = default;
    HazardReader &operator=(HazardReader &&) noexcept = default;
    HazardReader(const HazardReader &) = delete;
    HazardReader &operator=(const HazardReader &) = delete;
    [[nodiscard]] HazardGuard acquire() const;

  private:
    explicit HazardReader(std::shared_ptr<detail::HazardSlot> slot) noexcept;
    std::shared_ptr<detail::HazardSlot> slot_;
    friend class detail::HazardDomain;
};
} // namespace read_mostly
