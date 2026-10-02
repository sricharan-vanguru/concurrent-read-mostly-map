#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace read_mostly {
class SnapshotBuilder;

// Owned operations, applied in order. Copies are independent; no input string
// view is retained. A transaction may be reused against different snapshots.
class UpdateTransaction final {
  public:
    UpdateTransaction();
    ~UpdateTransaction();
    UpdateTransaction(const UpdateTransaction &);
    UpdateTransaction &operator=(const UpdateTransaction &);
    UpdateTransaction(UpdateTransaction &&) noexcept;
    UpdateTransaction &operator=(UpdateTransaction &&) noexcept;
    void insert_or_assign(std::string key, std::string value);
    void erase(std::string_view key);
    void clear();
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Impl &writable();
    friend class SnapshotBuilder;
};
} // namespace read_mostly
