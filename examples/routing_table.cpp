#include "routing_table.hpp"
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace example {
RouteView::RouteView(read_mostly::Snapshot snapshot) : snapshot_(std::move(snapshot)) {}
std::optional<std::string> RouteView::resolve(std::string_view path) const {
    return snapshot_.find_copy(path);
}
std::uint64_t RouteView::version() const noexcept { return snapshot_.version(); }
RouteView RoutingTable::acquire() const { return RouteView(map_.acquire_snapshot()); }
read_mostly::CommitResult RoutingTable::reload(std::span<const Route> routes,
                                               std::optional<std::uint64_t> expected_version) {
    std::vector<read_mostly::ReadMostlyMap::Entry> entries;
    entries.reserve(routes.size());
    std::unordered_set<std::string> paths;
    for (const auto &route : routes) {
        if (route.path.empty() || route.path.front() != '/' ||
            route.path.find_first_of("?# \t\r\n") != std::string::npos ||
            route.path.find('\0') != std::string::npos || route.destination.empty() ||
            route.destination.find_first_of("\r\n") != std::string::npos ||
            route.destination.find('\0') != std::string::npos || !paths.insert(route.path).second) {
            throw std::invalid_argument("invalid or duplicate exact route");
        }
        entries.emplace_back(route.path, route.destination);
    }
    return map_.replace_all(entries, expected_version);
}
} // namespace example
