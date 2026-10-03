#pragma once

#include "read_mostly/telemetry.hpp"
#include <chrono>
#include <memory>
#include <vector>

namespace read_mostly {
// Writer-side weak tracking: never extends snapshot lifetime.
class RetentionRegistry final {
  public:
    explicit RetentionRegistry(MapOptions options);
    bool admit(std::weak_ptr<const void> lifetime, std::size_t payload, std::uint64_t version);
    void inspect(MapStatistics &statistics);
    [[nodiscard]] bool enabled() const noexcept;

  private:
    struct Record {
        std::weak_ptr<const void> lifetime;
        std::size_t payload;
        std::uint64_t version;
        std::chrono::steady_clock::time_point published_at;
    };
    MapOptions options_;
    bool enabled_;
    std::vector<Record> records_;
    void sweep();
};
} // namespace read_mostly
