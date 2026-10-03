#pragma once
#include "read_mostly/read_mostly_map.hpp"
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace example {
struct Route {
    std::string path;
    std::string destination;
};
// Exact-path routing only: no prefix matching, URL parsing, or networking.
// Readers reuse the owning view for all decisions in one request.
class RouteView final {
  public:
    explicit RouteView(read_mostly::Snapshot snapshot);
    [[nodiscard]] std::optional<std::string> resolve(std::string_view path) const;
    [[nodiscard]] std::uint64_t version() const noexcept;

  private:
    read_mostly::Snapshot snapshot_;
};
class RoutingTable final {
  public:
    [[nodiscard]] RouteView acquire() const;
    // Invalid/duplicate routes throw before touching the published table.
    [[nodiscard]] read_mostly::CommitResult
    reload(std::span<const Route> routes,
           std::optional<std::uint64_t> expected_version = std::nullopt);

  private:
    read_mostly::ReadMostlyMap map_;
};
} // namespace example
