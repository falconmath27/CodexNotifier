#pragma once

#include <stdexcept>
#include <string_view>

namespace dimepenny::detail {

// Validate metadata without silently rewriting identities or display names.
inline void requireText(std::string_view value)
{
    if (value.empty() || value.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        throw std::invalid_argument("Domain text must not be empty or whitespace-only");
    }
    for (const unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7f) {
            throw std::invalid_argument("Domain text must not contain control characters");
        }
    }
}

}  // namespace dimepenny::detail
