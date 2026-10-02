#include "read_mostly/snapshot_builder.hpp"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using namespace read_mostly;

void check(bool condition) {
    if (!condition) {
        throw std::runtime_error("snapshot assertion failed");
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
        const Snapshot empty;
        check(empty.size() == 0 && empty.version() == 0 && !empty.find_copy("missing"));
        SnapshotBuilder builder;
        UpdateTransaction transaction;
        std::string key = "mode";
        transaction.insert_or_assign(key, "fast");
        key = "changed";
        const auto first = builder.build(empty, transaction);
        check(first.find_copy("mode") == "fast" && !empty.contains("mode"));
        check(first.payload_bytes() == 8 && first.version() == 1);
        const auto *retained_value = first.find("mode");
        UpdateTransaction edit;
        edit.insert_or_assign("mode", "slow");
        edit.insert_or_assign("erase-me", "value");
        edit.erase("erase-me");
        const auto second = builder.build(first, edit);
        check(second.size() == 1 && second.find_copy("mode") == "slow");
        check(*retained_value == "fast" && second.version() == 2);
        UpdateTransaction reset;
        reset.insert_or_assign("before", "clear");
        reset.clear();
        reset.insert_or_assign("after", "clear");
        check(builder.build(second, reset).size() == 1);
        const auto old_owner = [&] {
            auto temporary = builder.build(empty, transaction);
            return Snapshot(temporary);
        }();
        check(old_owner.find_copy("mode") == "fast");
        auto copied = transaction;
        copied.clear();
        check(builder.build(empty, copied).size() == 0 && transaction.size() == 1);
        auto moved = std::move(copied);
        check(copied.size() == 0 && moved.size() == 2);
        copied.insert_or_assign("reuse", "yes");
        check(builder.build(empty, copied).contains("reuse"));
        UpdateTransaction binary;
        binary.insert_or_assign(std::string("a\0b", 3), std::string("v\0x", 3));
        const auto binary_snapshot = builder.build(empty, binary);
        check(binary_snapshot.find_copy(std::string_view("a\0b", 3)) == std::string("v\0x", 3));
        rejects([&] { (void)SnapshotBuilder({0, 100, 10}).build(empty, transaction); });
        rejects([&] { (void)SnapshotBuilder({10, 7, 10}).build(empty, transaction); });
        rejects([&] { (void)SnapshotBuilder({10, 100, 0}).build(empty, transaction); });
        check(first.find_copy("mode") == "fast" && transaction.size() == 1);
        check(SnapshotBuilder({1, 8, 1}).build(empty, transaction).payload_bytes() == 8);
        check(builder.build(first, UpdateTransaction{}).version() == 2);
        std::atomic<bool> correct{true};
        std::vector<std::jthread> readers;
        for (int index = 0; index < 8; ++index) {
            readers.emplace_back([&] {
                for (int iteration = 0; iteration < 10000; ++iteration) {
                    if (first.find_copy("mode") != "fast" || first.size() != 1) {
                        correct.store(false, std::memory_order_relaxed);
                    }
                }
            });
        }
        readers.clear(); // jthread destruction joins before checking results.
        check(correct.load(std::memory_order_relaxed));
        std::cout << "Snapshot, transaction, limits, lifetime, and parallel-read tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
