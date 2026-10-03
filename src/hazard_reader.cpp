#include "hazard_domain.hpp"
#include <stdexcept>
#include <utility>

namespace read_mostly {
HazardReader::HazardReader(std::shared_ptr<detail::HazardSlot> slot) noexcept
    : slot_(std::move(slot)) {}
HazardGuard HazardReader::acquire() const {
    if (!slot_) {
        throw std::logic_error("moved-from hazard reader");
    }
    bool expected = false;
    if (!slot_->in_use.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        throw std::logic_error("reader already has a guard; register another reader for nesting");
    }
    return HazardGuard(slot_, slot_->domain->protect(*slot_));
}
HazardGuard::HazardGuard(std::shared_ptr<detail::HazardSlot> slot,
                         const Snapshot *snapshot) noexcept
    : slot_(std::move(slot)), snapshot_(snapshot) {}
HazardGuard::~HazardGuard() { release(); }
HazardGuard::HazardGuard(HazardGuard &&other) noexcept
    : slot_(std::move(other.slot_)), snapshot_(std::exchange(other.snapshot_, nullptr)) {}
HazardGuard &HazardGuard::operator=(HazardGuard &&other) noexcept {
    if (this != &other) {
        release();
        slot_ = std::move(other.slot_);
        snapshot_ = std::exchange(other.snapshot_, nullptr);
    }
    return *this;
}
void HazardGuard::release() noexcept {
    if (slot_) {
        slot_->protected_snapshot.store(nullptr, std::memory_order_seq_cst);
        slot_->in_use.store(false, std::memory_order_release);
        slot_.reset();
        snapshot_ = nullptr;
    }
}
const Snapshot &HazardGuard::view() const {
    if (!slot_) {
        throw std::logic_error("moved-from hazard guard");
    }
    return *snapshot_;
}
const std::string *HazardGuard::find(std::string_view key) const { return view().find(key); }
std::optional<std::string> HazardGuard::find_copy(std::string_view key) const {
    return view().find_copy(key);
}
Snapshot HazardGuard::copy_snapshot() const { return view(); }
std::uint64_t HazardGuard::version() const { return view().version(); }
std::size_t HazardGuard::size() const { return view().size(); }
} // namespace read_mostly
