#include "read_mostly/read_mostly_map.hpp"
#include "version.hpp"
#include <algorithm>
#include <atomic>
#include <barrier>
#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace read_mostly;
void check(bool condition) {
    if (!condition) {
        throw std::runtime_error("reference/lifecycle assertion failed");
    }
}
void concurrent_history() {
    struct Work {
        UpdateTransaction transaction;
        int kind = 0;
        std::string key;
        std::string value;
        std::uint64_t committed_version = 0;
        std::optional<Snapshot> observed;
    };
    std::vector<Work> work(120);
    for (std::size_t index = 0; index < work.size(); ++index) {
        auto &item = work[index];
        item.kind = index % 17 == 0 ? 0 : (index % 3 == 0 ? 1 : 2);
        item.key = std::to_string(index % 16);
        item.value = std::to_string(index);
        if (item.kind == 0) {
            item.transaction.clear();
        } else if (item.kind == 1) {
            item.transaction.erase(item.key);
        } else {
            item.transaction.insert_or_assign(item.key, item.value);
        }
    }
    ReadMostlyMap map;
    std::barrier start(5);
    std::vector<std::jthread> writers;
    for (std::size_t writer = 0; writer < 4; ++writer) {
        writers.emplace_back([&, writer] {
            start.arrive_and_wait();
            for (std::size_t index = writer; index < work.size(); index += 4) {
                auto &item = work[index];
                item.committed_version = map.commit(item.transaction).version;
                // Another writer may publish before this load. Validate against
                // the recorded version, never against this writer's own result.
                item.observed = map.acquire_snapshot();
            }
        });
    }
    start.arrive_and_wait();
    writers.clear();
    using Model = std::unordered_map<std::string, std::string>;
    std::vector<const Work *> ordered;
    for (const auto &item : work) {
        ordered.push_back(&item);
    }
    std::sort(ordered.begin(), ordered.end(), [](const Work *left, const Work *right) {
        return left->committed_version < right->committed_version;
    });
    std::vector<Model> history(1);
    for (const auto *item : ordered) {
        check(item->committed_version == history.size());
        auto state = history.back();
        if (item->kind == 0) {
            state.clear();
        } else if (item->kind == 1) {
            state.erase(item->key);
        } else {
            state.insert_or_assign(item->key, item->value);
        }
        history.push_back(std::move(state));
    }
    for (const auto &item : work) {
        const auto &snapshot = *item.observed;
        const auto &expected = history.at(static_cast<std::size_t>(snapshot.version()));
        check(snapshot.size() == expected.size());
        for (int index = 0; index < 16; ++index) {
            const auto key = std::to_string(index);
            const auto found = expected.find(key);
            check(snapshot.find_copy(key) == (found == expected.end()
                                                  ? std::nullopt
                                                  : std::optional<std::string>(found->second)));
        }
    }
    check(map.version() == work.size());
}
int main() {
    try {
        concurrent_history();
        check(detail::next_version(0) == 1);
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        check(detail::next_version(maximum - 1) == maximum);
        bool overflow = false;
        try {
            (void)detail::next_version(maximum);
        } catch (const std::overflow_error &) {
            overflow = true;
        }
        check(overflow);

        // Reproducible mixed batches compared with a separate sequential model.
        std::mt19937 random(20261002);
        ReadMostlyMap map;
        std::unordered_map<std::string, std::string> model;
        for (int step = 0; step < 500; ++step) {
            const auto old = map.acquire_snapshot();
            const auto old_model = model;
            UpdateTransaction batch;
            for (int operation = 0; operation < 4; ++operation) {
                const auto key = std::to_string(random() % 16);
                switch (random() % 5) {
                case 0:
                    batch.clear();
                    model.clear();
                    break;
                case 1:
                    batch.erase(key);
                    model.erase(key);
                    break;
                default: {
                    const auto value = std::to_string(random());
                    batch.insert_or_assign(key, value);
                    model.insert_or_assign(key, value);
                    break;
                }
                }
            }
            const auto result = map.commit(batch, old.version());
            check(result.status == CommitStatus::committed && result.version == old.version() + 1);
            const auto current = map.acquire_snapshot();
            check(current.size() == model.size() && old.size() == old_model.size());
            for (int index = 0; index < 16; ++index) {
                const auto key = std::to_string(index);
                const auto it = model.find(key);
                const auto old_it = old_model.find(key);
                check(current.find_copy(key) ==
                      (it == model.end() ? std::nullopt : std::optional<std::string>(it->second)));
                check(old.find_copy(key) == (old_it == old_model.end()
                                                 ? std::nullopt
                                                 : std::optional<std::string>(old_it->second)));
            }
        }
        // Race close against writers. Every success contributes exactly one
        // version; after close returns all subsequent commits must be rejected.
        ReadMostlyMap closing;
        std::barrier start(5);
        std::atomic<std::uint64_t> successes{0};
        std::atomic<bool> correct{true};
        std::vector<std::jthread> writers;
        for (int writer = 0; writer < 4; ++writer) {
            writers.emplace_back([&, writer] {
                UpdateTransaction batch;
                batch.insert_or_assign(std::to_string(writer), "value");
                start.arrive_and_wait();
                for (int index = 0; index < 100; ++index) {
                    const auto result = closing.commit(batch);
                    if (result.status == CommitStatus::committed) {
                        ++successes;
                    } else if (result.status != CommitStatus::closed) {
                        correct = false;
                    }
                }
            });
        }
        start.arrive_and_wait();
        closing.close();
        writers.clear();
        check(correct && closing.version() == successes);
        check(closing.commit(UpdateTransaction{}).status == CommitStatus::closed);
        // Deterministic close-before-writer ordering.
        std::jthread late([&] {
            if (closing.commit(UpdateTransaction{}).status != CommitStatus::closed) {
                correct = false;
            }
        });
        late.join();
        check(correct);
        std::cout << "Seeded reference model, close races, and overflow boundaries passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
