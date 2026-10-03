#pragma once

#include "read_mostly/snapshot.hpp"
#include "retention_registry.hpp"
#include <atomic>
#include <memory>
#include <stdexcept>
#include <utility>

namespace read_mostly {
// Publication and lifetime are deliberately coupled: load returns ownership,
// so replacing current cannot free data underneath an existing reader.
class OwningPublication final {
  public:
    explicit OwningPublication(RetentionRegistry &registry)
        : current_(std::make_shared<const Snapshot>()) {
        const auto initial = acquire();
        if (!registry.admit(initial.data_, initial.payload_bytes(), initial.version())) {
            throw std::invalid_argument("live snapshot budget must allow the initial snapshot");
        }
    }
    [[nodiscard]] Snapshot acquire() const { return *current_.load(std::memory_order_acquire); }
    bool publish(Snapshot snapshot, RetentionRegistry &registry) {
        // Allocation completes before the store; failures preserve current.
        auto owner = std::make_shared<const Snapshot>(std::move(snapshot));
        if (!registry.admit(owner->data_, owner->payload_bytes(), owner->version())) {
            return false;
        }
        current_.store(std::move(owner), std::memory_order_release);
        return true;
    }

  private:
    std::atomic<std::shared_ptr<const Snapshot>> current_;
};
} // namespace read_mostly
