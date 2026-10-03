#include "hazard_domain.hpp"
#include <algorithm>
#include <stdexcept>

namespace read_mostly::detail {
HazardSlot::HazardSlot(std::shared_ptr<HazardDomain> owner) : domain(std::move(owner)) {
    if (!protected_snapshot.is_lock_free() || !in_use.is_lock_free()) {
        throw std::runtime_error("hazard backend requires lock-free pointer and boolean atomics");
    }
}
HazardDomain::HazardDomain(RetentionRegistry &registry)
    : owner_(std::make_unique<const Snapshot>()), current_(owner_.get()) {
    if (!current_.is_lock_free()) {
        throw std::runtime_error("hazard backend requires lock-free pointer atomics");
    }
    if (!registry.admit(owner_->data_, owner_->payload_bytes(), owner_->version())) {
        throw std::invalid_argument("live snapshot budget must allow the initial snapshot");
    }
}
HazardReader HazardDomain::register_reader() {
    auto slot = std::make_shared<HazardSlot>(shared_from_this());
    const std::lock_guard lock(slots_mutex_);
    std::erase_if(slots_, [](const auto &record) { return record.expired(); });
    slots_.push_back(slot);
    return HazardReader(std::move(slot));
}
const Snapshot *HazardDomain::protect(HazardSlot &slot) const noexcept {
    for (;;) {
        // Never dereference until validation succeeds. SC order prevents the
        // writer scan from missing protection when validation sees the old head.
        const auto *snapshot = current_.load(std::memory_order_seq_cst);
        slot.protected_snapshot.store(snapshot, std::memory_order_seq_cst);
        const auto *validated = current_.load(std::memory_order_seq_cst);
        if (snapshot == validated) {
            // Use the fresh pointer from validation, including address reuse.
            return validated;
        }
    }
}
Snapshot HazardDomain::current_snapshot() const { return *owner_; }
bool HazardDomain::publish(Snapshot snapshot, RetentionRegistry &registry) {
    collect();
    auto candidate = std::make_unique<const Snapshot>(std::move(snapshot));
    // Retirement bookkeeping must not allocate after the linearization point.
    retired_.reserve(retired_.size() + 1);
    if (!registry.admit(candidate->data_, candidate->payload_bytes(), candidate->version())) {
        return false;
    }
    auto previous = std::move(owner_);
    owner_ = std::move(candidate);
    current_.exchange(owner_.get(), std::memory_order_seq_cst);
    retired_.push_back(std::move(previous)); // Reserved storage, noexcept move.
    return true;
}
void HazardDomain::collect() {
    const std::lock_guard lock(slots_mutex_);
    std::erase_if(slots_, [](const auto &record) { return record.expired(); });
    std::erase_if(retired_, [&](const auto &snapshot) {
        for (const auto &record : slots_) {
            if (const auto slot = record.lock()) {
                if (slot->protected_snapshot.load(std::memory_order_seq_cst) == snapshot.get()) {
                    return false;
                }
            }
        }
        return true;
    });
}
std::size_t HazardDomain::retired_count() const noexcept { return retired_.size(); }
} // namespace read_mostly::detail
