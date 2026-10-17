#pragma once

#include <string_view>

namespace dimepenny {

[[nodiscard]] std::string_view applicationName() noexcept;
[[nodiscard]] std::string_view applicationVersion() noexcept;

}  // namespace dimepenny

