#include "routing_table.hpp"
#include <array>
#include <iostream>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
void check(bool value) {
    if (!value)
        throw std::runtime_error("integration/model assertion failed");
}
void routing_contract() {
    example::RoutingTable table;
    const std::array<example::Route, 1> first{{{"/api", "v1"}}};
    check(table.reload(first, 0).status == read_mostly::CommitStatus::committed);
    const auto retained = table.acquire();
    const std::array<example::Route, 1> second{{{"/api", "v2"}}};
    check(table.reload(second, 0).status == read_mostly::CommitStatus::version_conflict);
    check(table.reload(second, 1).status == read_mostly::CommitStatus::committed);
    check(retained.resolve("/api") == "v1");
    check(table.acquire().resolve("/api") == "v2");
    check(!table.acquire().resolve("/api/child"));
    for (const auto &invalid : std::vector<std::vector<example::Route>>{{{"relative", "x"}},
                                                                        {{"/a?b", "x"}},
                                                                        {{"/a", ""}},
                                                                        {{"/a", "x"}, {"/a", "y"}},
                                                                        {{"/a", "x\ny"}}}) {
        bool rejected = false;
        try {
            (void)table.reload(invalid);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected && table.acquire().version() == 2);
    }
    check(table.reload({}).status == read_mostly::CommitStatus::committed);
    check(!table.acquire().resolve("/api"));
    check(retained.resolve("/api") == "v1");
}
// Property-style reproducible streams; not coverage-guided fuzzing.
void model_stream(std::uint32_t seed, read_mostly::PublicationBackend backend) {
    read_mostly::MapOptions options;
    options.backend = backend;
    read_mostly::ReadMostlyMap map({}, options);
    std::mt19937 random(seed);
    std::unordered_map<std::string, std::string> model;
    for (int step = 0; step < 250; ++step) {
        const auto before = map.acquire_snapshot();
        const auto previous = model;
        const bool conflict = random() % 7 == 0;
        const auto expected = before.version() + (conflict ? 1U : 0U);
        read_mostly::CommitResult result{read_mostly::CommitStatus::closed, 0};
        if (random() % 4 == 0) {
            std::vector<read_mostly::ReadMostlyMap::Entry> entries;
            model.clear();
            for (auto count = random() % 12; count > 0; --count) {
                const auto key = std::to_string(random() % 24);
                const auto value = std::to_string(random());
                entries.emplace_back(key, value);
                model.insert_or_assign(key, value);
            }
            result = map.replace_all(entries, expected);
        } else {
            read_mostly::UpdateTransaction batch;
            for (auto count = random() % 8; count > 0; --count) {
                const auto key = std::to_string(random() % 24);
                const auto operation = random() % 8;
                if (operation == 0) {
                    batch.clear();
                    model.clear();
                } else if (operation < 3) {
                    batch.erase(key);
                    model.erase(key);
                } else {
                    const auto value = std::to_string(random());
                    batch.insert_or_assign(key, value);
                    model.insert_or_assign(key, value);
                }
            }
            result = map.commit(batch, expected);
        }
        if (conflict)
            model = previous;
        check(result.status == (conflict ? read_mostly::CommitStatus::version_conflict
                                         : read_mostly::CommitStatus::committed));
        const auto current = map.acquire_snapshot();
        check(current.version() == before.version() + (conflict ? 0U : 1U));
        check(current.size() == model.size() && before.size() == previous.size());
        for (int index = 0; index < 24; ++index) {
            const auto key = std::to_string(index);
            const auto value = model.find(key);
            const auto old = previous.find(key);
            check(
                current.find_copy(key) ==
                (value == model.end() ? std::nullopt : std::optional<std::string>(value->second)));
            check(before.find_copy(key) ==
                  (old == previous.end() ? std::nullopt : std::optional<std::string>(old->second)));
        }
    }
    map.close();
    check(map.commit({}).status == read_mostly::CommitStatus::closed);
    check(map.replace_all({}).status == read_mostly::CommitStatus::closed);
}
} // namespace
int main() {
    try {
        routing_contract();
        for (std::uint32_t seed = 0; seed < 16; ++seed) {
            for (const auto backend : {read_mostly::PublicationBackend::shared_ownership,
                                       read_mostly::PublicationBackend::experimental_hazard}) {
                model_stream(20261003U + seed, backend);
            }
        }
        std::cout << "Routing contracts and 32 reproducible model streams passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
