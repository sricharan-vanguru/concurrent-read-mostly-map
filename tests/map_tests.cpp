#include "read_mostly/read_mostly_map.hpp"
#include <atomic>
#include <barrier>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace read_mostly;
void check(bool value) {
    if (!value) {
        throw std::runtime_error("map assertion failed");
    }
}
template <class Function> void rejects(Function function) {
    try {
        function();
    } catch (const std::length_error &) {
        return;
    }
    throw std::runtime_error("expected length_error");
}
int main() {
    try {
        ReadMostlyMap map;
        check(map.version() == 0 && map.size() == 0 && !map.find_copy("none"));
        UpdateTransaction batch;
        batch.insert_or_assign("a", "1");
        batch.insert_or_assign("b", "1");
        check(map.commit(batch, 0).status == CommitStatus::committed);
        const auto old = map.acquire_snapshot();
        check(map.commit(batch, 0).status == CommitStatus::version_conflict);
        check(map.version() == 1 && old.find_copy("a") == "1");
        const std::vector<ReadMostlyMap::Entry> replacement{{"c", "2"}, {"c", "3"}};
        check(map.replace_all(replacement, 1).version == 2);
        check(!map.contains("a") && map.find_copy("c") == "3" && old.size() == 2);
        check(map.commit(UpdateTransaction{}).version == 3);
        map.close();
        map.close();
        check(map.commit(batch).status == CommitStatus::closed);
        check(map.replace_all(replacement, 0).status == CommitStatus::closed);
        check(map.version() == 3 && map.contains("c"));
        const auto retained = [] {
            ReadMostlyMap temporary;
            UpdateTransaction edit;
            edit.insert_or_assign("kept", "alive");
            check(temporary.commit(edit).status == CommitStatus::committed);
            return temporary.acquire_snapshot();
        }();
        check(retained.find_copy("kept") == "alive");
        ReadMostlyMap limited({1, 4, 1});
        rejects([&] { (void)limited.commit(batch); });
        check(limited.version() == 0);
        const std::vector<ReadMostlyMap::Entry> one{{"a", "123"}};
        check(limited.replace_all(one).version == 1);
        const std::vector<ReadMostlyMap::Entry> too_large{{"a", "1234"}};
        rejects([&] { (void)limited.replace_all(too_large); });
        check(limited.version() == 1 && limited.find_copy("a") == "123");
        check(limited.replace_all({}).version == 2 && limited.size() == 0);

        // Writers start together; all independently inserted keys must survive.
        ReadMostlyMap multi;
        std::barrier start(5);
        std::atomic<bool> correct{true};
        std::vector<std::jthread> writers;
        for (int writer = 0; writer < 4; ++writer) {
            writers.emplace_back([&, writer] {
                start.arrive_and_wait();
                for (int index = 0; index < 30; ++index) {
                    UpdateTransaction edit;
                    edit.insert_or_assign(std::to_string(writer) + ":" + std::to_string(index),
                                          "ok");
                    if (multi.commit(edit).status != CommitStatus::committed) {
                        correct = false;
                    }
                }
            });
        }
        start.arrive_and_wait();
        writers.clear();
        check(correct && multi.size() == 120 && multi.version() == 120);
        for (int writer = 0; writer < 4; ++writer) {
            for (int index = 0; index < 30; ++index) {
                check(multi.find_copy(std::to_string(writer) + ":" + std::to_string(index)) ==
                      "ok");
            }
        }
        // Only one writer can commit using a given expected version.
        ReadMostlyMap conditional;
        std::barrier conditional_start(3);
        std::atomic<int> successes{0};
        auto attempt = [&] {
            conditional_start.arrive_and_wait();
            if (conditional.commit(batch, 0).status == CommitStatus::committed) {
                ++successes;
            }
        };
        std::jthread first(attempt), second(attempt);
        conditional_start.arrive_and_wait();
        first.join();
        second.join();
        check(successes == 1 && conditional.version() == 1);

        // Every reader uses one handle for both fields, so a mixed batch is invalid.
        ReadMostlyMap observed;
        check(observed.commit(batch).version == 1);
        std::barrier read_start(5);
        std::vector<std::jthread> readers;
        for (int index = 0; index < 4; ++index) {
            readers.emplace_back([&] {
                read_start.arrive_and_wait();
                for (int iteration = 0; iteration < 2000; ++iteration) {
                    const auto view = observed.acquire_snapshot();
                    if (view.find_copy("a") != view.find_copy("b") || view.size() != 2) {
                        correct = false;
                    }
                }
            });
        }
        read_start.arrive_and_wait();
        for (int iteration = 0; iteration < 200; ++iteration) {
            UpdateTransaction edit;
            edit.insert_or_assign("a", std::to_string(iteration));
            edit.insert_or_assign("b", std::to_string(iteration));
            check(observed.commit(edit).status == CommitStatus::committed);
        }
        readers.clear();
        check(correct);
        std::cout << "Publication, conflict, limits, lifetime, and multi-writer tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
