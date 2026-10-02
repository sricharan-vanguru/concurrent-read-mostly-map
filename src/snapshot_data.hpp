#pragma once

#include "read_mostly/snapshot.hpp"
#include <functional>
#include <unordered_map>

namespace read_mostly {
struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view key) const noexcept {
        return std::hash<std::string_view>{}(key);
    }
};
struct Snapshot::Data {
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> entries;
    std::uint64_t version = 0;
    std::size_t payload_bytes = 0;
};
} // namespace read_mostly
