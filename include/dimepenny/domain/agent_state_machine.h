#pragma once

#include "dimepenny/domain/agent_event.h"
#include "dimepenny/domain/session.h"

namespace dimepenny {

enum class TransitionResult {
    Applied,
    WrongSession,
    DuplicateEvent,
    OutOfOrder,
    MissingSequence,
    InvalidTransition,
    ApprovalMismatch
};

class AgentStateMachine final {
public:
    // Local connection intent, not a claim that a bridge has connected.
    [[nodiscard]] static TransitionResult beginConnecting(Session& session) noexcept
    {
        if (session.state_ != AgentState::Unregistered
            && session.state_ != AgentState::Disconnected && session.state_ != AgentState::Error) {
            return TransitionResult::InvalidTransition;
        }
        session.pendingApprovalId_.reset();
        session.state_ = AgentState::Connecting;
        return TransitionResult::Applied;
    }

    [[nodiscard]] static constexpr std::optional<AgentState> nextState(
        AgentState state, AgentEventType event) noexcept
    {
        switch (state) {
        case AgentState::Unregistered:
        case AgentState::Disconnected:
            return std::nullopt;
        case AgentState::Connecting:
            if (event == AgentEventType::SessionConnected) return AgentState::Active;
            break;
        case AgentState::Active:
        case AgentState::Completed:
            if (event == AgentEventType::TaskStarted) return AgentState::Working;
            break;
        case AgentState::Working:
            if (event == AgentEventType::TaskCompleted) return AgentState::Completed;
            if (event == AgentEventType::ApprovalRequested) return AgentState::ApprovalNeeded;
            break;
        case AgentState::ApprovalNeeded:
            if (event == AgentEventType::ApprovalResolved) return AgentState::Working;
            break;
        case AgentState::Error:
            if (event == AgentEventType::SessionDisconnected) return AgentState::Disconnected;
            return std::nullopt;
        default:
            return std::nullopt;
        }
        if (event == AgentEventType::SessionDisconnected) return AgentState::Disconnected;
        if (event == AgentEventType::SessionError) return AgentState::Error;
        return std::nullopt;
    }

    [[nodiscard]] static TransitionResult apply(Session& session, const AgentEvent& event)
    {
        if (event.sessionId() != session.id_) return TransitionResult::WrongSession;
        if (session.acceptedEventIds_.contains(event.eventId())) return TransitionResult::DuplicateEvent;
        if (session.lastSequence_ && !event.sequence()) return TransitionResult::MissingSequence;
        if ((session.lastSequence_ && event.sequence() <= session.lastSequence_)
            || event.occurredAt() < session.createdAt_
            || (session.lastEventAt_ && event.occurredAt() < *session.lastEventAt_)) {
            return TransitionResult::OutOfOrder;
        }
        const auto next = nextState(session.state_, event.type());
        if (!next) return TransitionResult::InvalidTransition;
        if (event.type() == AgentEventType::ApprovalResolved
            && (!session.pendingApprovalId_
                || *session.pendingApprovalId_ != event.approval()->requestId)) {
            return TransitionResult::ApprovalMismatch;
        }

        // Prepare allocations before committing. Rejection never consumes an ID or sequence.
        std::optional<std::string> pending;
        if (event.type() == AgentEventType::ApprovalRequested) pending = event.approval()->requestId;
        session.acceptedEventIds_.insert(event.eventId());
        session.pendingApprovalId_.swap(pending);
        session.lastEventAt_ = event.occurredAt();
        session.lastSequence_ = event.sequence();
        session.state_ = *next;
        return TransitionResult::Applied;
    }
};

}  // namespace dimepenny
