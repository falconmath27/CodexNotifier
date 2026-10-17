#pragma once

#include "dimepenny/domain/session_id.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace dimepenny {

enum class AgentEventType {
    SessionConnected,
    TaskStarted,
    TaskCompleted,
    ApprovalRequested,
    ApprovalResolved,
    SessionDisconnected,
    SessionError
};

enum class ApprovalDecision { Approve, Deny };

// Correlation metadata only; routing, expiry and operation details are later tasks.
struct ApprovalInfo {
    std::string requestId;
    std::optional<ApprovalDecision> decision;
};

class AgentEvent final {
public:
    using Timestamp = std::chrono::system_clock::time_point;

    AgentEvent(std::string eventId, SessionId sessionId, AgentEventType type,
               Timestamp occurredAt, std::optional<std::uint64_t> sequence = std::nullopt,
               std::optional<ApprovalInfo> approval = std::nullopt)
        : eventId_(std::move(eventId)), sessionId_(std::move(sessionId)), type_(type),
          occurredAt_(occurredAt), sequence_(sequence), approval_(std::move(approval))
    {
        detail::requireText(eventId_);
        switch (type_) {
        case AgentEventType::ApprovalRequested:
        case AgentEventType::ApprovalResolved:
            if (!approval_) throw std::invalid_argument("Approval event requires correlation metadata");
            detail::requireText(approval_->requestId);
            if (approval_->decision.has_value() != (type_ == AgentEventType::ApprovalResolved)) {
                throw std::invalid_argument("Only resolved approvals require a decision");
            }
            if (approval_->decision && *approval_->decision != ApprovalDecision::Approve
                && *approval_->decision != ApprovalDecision::Deny) {
                throw std::invalid_argument("Unknown approval decision");
            }
            break;
        case AgentEventType::SessionConnected:
        case AgentEventType::TaskStarted:
        case AgentEventType::TaskCompleted:
        case AgentEventType::SessionDisconnected:
        case AgentEventType::SessionError:
            if (approval_) throw std::invalid_argument("Non-approval event cannot contain approval metadata");
            break;
        default:
            throw std::invalid_argument("Unknown agent event type");
        }
    }

    [[nodiscard]] const std::string& eventId() const noexcept { return eventId_; }
    [[nodiscard]] const SessionId& sessionId() const noexcept { return sessionId_; }
    [[nodiscard]] AgentEventType type() const noexcept { return type_; }
    [[nodiscard]] Timestamp occurredAt() const noexcept { return occurredAt_; }
    [[nodiscard]] std::optional<std::uint64_t> sequence() const noexcept { return sequence_; }
    [[nodiscard]] const std::optional<ApprovalInfo>& approval() const noexcept { return approval_; }

private:
    std::string eventId_;
    SessionId sessionId_;
    AgentEventType type_;
    Timestamp occurredAt_;
    std::optional<std::uint64_t> sequence_;
    std::optional<ApprovalInfo> approval_;
};

}  // namespace dimepenny
