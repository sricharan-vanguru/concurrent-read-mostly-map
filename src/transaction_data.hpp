#pragma once

#include "read_mostly/update_transaction.hpp"
#include <vector>

namespace read_mostly {
struct UpdateTransaction::Impl {
    enum class Kind { assign, erase, clear };
    struct Operation {
        Kind kind;
        std::string key;
        std::string value;
    };
    std::vector<Operation> operations;
};
} // namespace read_mostly
