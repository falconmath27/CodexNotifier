#pragma once

#include "dimepenny/domain/validation.h"

#include <compare>
#include <functional>
#include <string>
#include <utility>

namespace dimepenny {

// Application registration identity, distinct from a Codex ID or process ID.
class SessionId final {
public:
    explicit SessionId(std::string value) : value_(std::move(value))
    {
        detail::requireText(value_);
        if (value_.size() > 128 || value_.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.:")
                != std::string::npos) {
            throw std::invalid_argument("Session ID must be 1-128 ASCII identifier characters");
        }
    }

    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    auto operator<=>(const SessionId&) const = default;

private:
    std::string value_;
};

}  // namespace dimepenny

template <>
struct std::hash<dimepenny::SessionId> {
    std::size_t operator()(const dimepenny::SessionId& id) const noexcept
    {
        return std::hash<std::string>{}(id.value());
    }
};
