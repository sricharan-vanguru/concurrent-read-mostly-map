#pragma once

#include "read_mostly/snapshot.hpp"
#include <atomic>
#include <memory>
#include <utility>

namespace read_mostly {
// Publication and lifetime are deliberately coupled: load returns ownership,
// so replacing current cannot free data underneath an existing reader.
class OwningPublication final {
  public:
    OwningPublication() : current_(std::make_shared<const Snapshot>()) {}
    [[nodiscard]] Snapshot acquire() const { return *current_.load(std::memory_order_acquire); }
    void publish(Snapshot snapshot) {
        // Allocation completes before the store; failures preserve current.
        auto owner = std::make_shared<const Snapshot>(std::move(snapshot));
        current_.store(std::move(owner), std::memory_order_release);
    }

  private:
    std::atomic<std::shared_ptr<const Snapshot>> current_;
};
} // namespace read_mostly
