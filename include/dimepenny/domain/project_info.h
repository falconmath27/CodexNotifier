#pragma once

#include "dimepenny/domain/validation.h"

#include <filesystem>
#include <string>
#include <utility>

namespace dimepenny {

class ProjectInfo final {
public:
    ProjectInfo(std::string name, std::filesystem::path directory)
        : name_(std::move(name)), directory_(std::move(directory))
    {
        detail::requireText(name_);
        if (directory_.empty() || !directory_.is_absolute()
            || directory_.native().find(std::filesystem::path::value_type{})
                != std::filesystem::path::string_type::npos) {
            throw std::invalid_argument("Project directory must be an absolute path without NUL");
        }
        // Existence, canonicalization and access checks belong to filesystem adapters.
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }
    bool operator==(const ProjectInfo&) const = default;

private:
    std::string name_;
    std::filesystem::path directory_;
};

}  // namespace dimepenny
