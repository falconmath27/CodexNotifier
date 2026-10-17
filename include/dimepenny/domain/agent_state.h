#pragma once

#include <string_view>

namespace dimepenny {

enum class AgentState {
    Unregistered,
    Connecting,
    Active,
    Working,
    ApprovalNeeded,
    Completed,
    Error,
    Disconnected
};

[[nodiscard]] constexpr std::string_view toString(AgentState state) noexcept
{
    switch (state) {
    case AgentState::Unregistered: return "Unregistered";
    case AgentState::Connecting: return "Connecting";
    case AgentState::Active: return "Active";
    case AgentState::Working: return "Working";
    case AgentState::ApprovalNeeded: return "ApprovalNeeded";
    case AgentState::Completed: return "Completed";
    case AgentState::Error: return "Error";
    case AgentState::Disconnected: return "Disconnected";
    }
    return "Unknown";
}

}  // namespace dimepenny
