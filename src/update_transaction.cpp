#include "transaction_data.hpp"
#include <utility>

namespace read_mostly {
UpdateTransaction::UpdateTransaction() : impl_(std::make_unique<Impl>()) {}
UpdateTransaction::~UpdateTransaction() = default;
UpdateTransaction::UpdateTransaction(const UpdateTransaction &other)
    : impl_(other.impl_ ? std::make_unique<Impl>(*other.impl_) : std::make_unique<Impl>()) {}
UpdateTransaction &UpdateTransaction::operator=(const UpdateTransaction &other) {
    if (this != &other) {
        UpdateTransaction copy(other);
        impl_.swap(copy.impl_);
    }
    return *this;
}
UpdateTransaction::UpdateTransaction(UpdateTransaction &&) noexcept = default;
UpdateTransaction &UpdateTransaction::operator=(UpdateTransaction &&) noexcept = default;
UpdateTransaction::Impl &UpdateTransaction::writable() {
    if (!impl_) {
        impl_ = std::make_unique<Impl>();
    }
    return *impl_;
}
void UpdateTransaction::insert_or_assign(std::string key, std::string value) {
    writable().operations.push_back({Impl::Kind::assign, std::move(key), std::move(value)});
}
void UpdateTransaction::erase(std::string_view key) {
    writable().operations.push_back({Impl::Kind::erase, std::string(key), {}});
}
void UpdateTransaction::clear() { writable().operations.push_back({Impl::Kind::clear, {}, {}}); }
std::size_t UpdateTransaction::size() const noexcept {
    return impl_ ? impl_->operations.size() : 0;
}
} // namespace read_mostly
