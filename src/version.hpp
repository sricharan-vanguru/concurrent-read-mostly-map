#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace read_mostly::detail {
inline std::uint64_t next_version(std::uint64_t current) {
    if (current == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("snapshot version exhausted");
    }
    return current + 1;
}
} // namespace read_mostly::detail
