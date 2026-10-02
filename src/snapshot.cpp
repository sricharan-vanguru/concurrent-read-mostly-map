#include "snapshot_data.hpp"
#include <utility>

namespace read_mostly {
Snapshot::Snapshot() : data_(std::make_shared<const Data>()) {}
Snapshot::Snapshot(std::shared_ptr<const Data> data) noexcept : data_(std::move(data)) {}
const std::string *Snapshot::find(std::string_view key) const {
    const auto it = data_->entries.find(key);
    return it == data_->entries.end() ? nullptr : &it->second;
}
std::optional<std::string> Snapshot::find_copy(std::string_view key) const {
    const auto *value = find(key);
    return value ? std::optional<std::string>(*value) : std::nullopt;
}
bool Snapshot::contains(std::string_view key) const { return find(key) != nullptr; }
std::size_t Snapshot::size() const noexcept { return data_->entries.size(); }
std::size_t Snapshot::payload_bytes() const noexcept { return data_->payload_bytes; }
std::uint64_t Snapshot::version() const noexcept { return data_->version; }
} // namespace read_mostly
