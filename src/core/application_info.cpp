#include "dimepenny/core/application_info.h"

#include "dimepenny/core/build_config.h"

namespace dimepenny {

std::string_view applicationName() noexcept
{
    return DIMEPENNY_PROJECT_NAME;
}

std::string_view applicationVersion() noexcept
{
    return DIMEPENNY_PROJECT_VERSION;
}

}  // namespace dimepenny

