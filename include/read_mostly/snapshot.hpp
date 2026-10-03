#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace read_mostly {
class SnapshotBuilder;
class OwningPublication;
namespace detail {
class HazardDomain;
}

// A cheap, owning handle. Copies share immutable storage; even references
// returned by find remain valid while any copy of this snapshot stays alive.
class Snapshot final {
  public:
    Snapshot();
    // Rvalues also copy ownership: every snapshot remains a valid read handle.
    Snapshot(const Snapshot &) noexcept = default;
    Snapshot &operator=(const Snapshot &) noexcept = default;
    [[nodiscard]] std::optional<std::string> find_copy(std::string_view key) const;
    [[nodiscard]] const std::string *find(std::string_view key) const;
    [[nodiscard]] bool contains(std::string_view key) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t payload_bytes() const noexcept;
    [[nodiscard]] std::uint64_t version() const noexcept;

  private:
    struct Data;
    explicit Snapshot(std::shared_ptr<const Data> data) noexcept;
    std::shared_ptr<const Data> data_;
    friend class SnapshotBuilder;
    friend class OwningPublication;
    friend class detail::HazardDomain;
};
} // namespace read_mostly
