#pragma once

#include "dimepenny/domain/agent_state.h"
#include "dimepenny/domain/pet_identity.h"
#include "dimepenny/domain/project_info.h"
#include "dimepenny/domain/session_id.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_set>

namespace dimepenny {

class Session final {
public:
    using Timestamp = std::chrono::system_clock::time_point;

    Session(SessionId id, ProjectInfo project, PetIdentity pet, std::string terminalType,
            Timestamp createdAt, std::optional<std::string> codexSessionId = std::nullopt,
            std::optional<std::uint32_t> processId = std::nullopt)
        : id_(std::move(id)), project_(std::move(project)), pet_(std::move(pet)),
          terminalType_(std::move(terminalType)), createdAt_(createdAt),
          codexSessionId_(std::move(codexSessionId)), processId_(processId)
    {
        detail::requireText(terminalType_);
        if (codexSessionId_) detail::requireText(*codexSessionId_);
        if (processId_ && *processId_ == 0) {
            throw std::invalid_argument("Process ID must be positive when provided");
        }
    }

    [[nodiscard]] const SessionId& id() const noexcept { return id_; }
    [[nodiscard]] const ProjectInfo& project() const noexcept { return project_; }
    [[nodiscard]] const PetIdentity& pet() const noexcept { return pet_; }
    [[nodiscard]] const std::string& terminalType() const noexcept { return terminalType_; }
    [[nodiscard]] Timestamp createdAt() const noexcept { return createdAt_; }
    [[nodiscard]] const std::optional<std::string>& codexSessionId() const noexcept { return codexSessionId_; }
    [[nodiscard]] std::optional<std::uint32_t> processId() const noexcept { return processId_; }
    [[nodiscard]] AgentState state() const noexcept { return state_; }

private:
    friend class AgentStateMachine;
    SessionId id_;
    ProjectInfo project_;
    PetIdentity pet_;
    std::string terminalType_;
    Timestamp createdAt_;
    std::optional<std::string> codexSessionId_;
    std::optional<std::uint32_t> processId_;
    // Only AgentStateMachine can mutate lifecycle state and its ordering metadata.
    AgentState state_{AgentState::Unregistered};
    std::optional<Timestamp> lastEventAt_;
    std::optional<std::uint64_t> lastSequence_;
    std::optional<std::string> pendingApprovalId_;
    std::unordered_set<std::string> acceptedEventIds_;
};

}  // namespace dimepenny
