#pragma once

#include "dimepenny/domain/validation.h"

#include <cstdint>
#include <string>
#include <utility>

namespace dimepenny {

struct PetColor {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    bool operator==(const PetColor&) const = default;
};

class PetIdentity final {
public:
    PetIdentity(std::string id, std::string name, std::string appearanceKey, PetColor color)
        : id_(std::move(id)), name_(std::move(name)),
          appearanceKey_(std::move(appearanceKey)), color_(color)
    {
        detail::requireText(id_);
        detail::requireText(name_);
        detail::requireText(appearanceKey_);
    }

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& appearanceKey() const noexcept { return appearanceKey_; }
    [[nodiscard]] PetColor color() const noexcept { return color_; }
    bool operator==(const PetIdentity&) const = default;

private:
    std::string id_;
    std::string name_;
    std::string appearanceKey_;
    PetColor color_;
};

}  // namespace dimepenny
