#include "read_mostly/read_mostly_map.hpp"
#include <array>
#include <iostream>

int main() {
    read_mostly::ReadMostlyMap config;
    const std::array<read_mostly::ReadMostlyMap::Entry, 2> initial{
        {{"checkout.enabled", "false"}, {"checkout.endpoint", "stable"}}};
    if (config.replace_all(initial, 0).status != read_mostly::CommitStatus::committed)
        return 1;
    const auto request = config.acquire_snapshot();
    // Parse and validate external configuration before building this batch.
    read_mostly::UpdateTransaction reload;
    reload.insert_or_assign("checkout.enabled", "true");
    reload.insert_or_assign("checkout.endpoint", "canary");
    if (config.commit(reload, request.version()).status != read_mostly::CommitStatus::committed)
        return 1;
    const auto next_request = config.acquire_snapshot();
    if (request.find_copy("checkout.enabled") != "false" ||
        next_request.find_copy("checkout.enabled") != "true" ||
        next_request.find_copy("checkout.endpoint") != "canary")
        return 1;
    config.close();
    std::cout << "In-flight request retained v" << request.version() << "; next request uses v"
              << next_request.version() << '\n';
}
