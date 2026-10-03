#include "routing_table.hpp"
#include <array>
#include <iostream>

int main() {
    example::RoutingTable table;
    const std::array<example::Route, 2> routes{{{"/health", "local"}, {"/checkout", "payments"}}};
    if (table.reload(routes, 0).status != read_mostly::CommitStatus::committed)
        return 1;
    const auto request = table.acquire();
    const auto destination = request.resolve("/checkout");
    if (!destination || *destination != "payments" || request.resolve("/missing"))
        return 1;
    std::cout << "Exact route /checkout -> " << *destination << '\n';
}
