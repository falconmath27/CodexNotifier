#pragma once

#include "dimepenny/domain/agent_state_machine.h"

#include <algorithm>
#include <variant>
#include <vector>
#include <unordered_map>

namespace dimepenny {

enum class RegistryResult {
    Registered, Deactivated, Reconnecting, NotFound, AlreadyInactive,
    DuplicateSessionId, DuplicateCodexId, DuplicatePetId, DuplicateAppearance,
    CapacityReached, InvalidState, InactiveSession
};

using RegistryEventResult = std::variant<RegistryResult, TransitionResult>;

// Owns sessions. Callers serialize access; only const views escape the registry.
class SessionRegistry final {
public:
    static constexpr std::size_t capacity = 10;

    [[nodiscard]] RegistryResult registerSession(Session session)
    {
        if (entries_.contains(session.id())) return RegistryResult::DuplicateSessionId;
        // Reserve identity even for deactivated records so reconnect restores the same pet.
        for (const auto& [id, entry] : entries_) {
            (void)id;
            const auto& existing = entry.session;
            if (session.codexSessionId() && session.codexSessionId() == existing.codexSessionId())
                return RegistryResult::DuplicateCodexId;
            if (session.pet().id() == existing.pet().id()) return RegistryResult::DuplicatePetId;
            if (session.pet().appearanceKey() == existing.pet().appearanceKey()
                && session.pet().color() == existing.pet().color()) return RegistryResult::DuplicateAppearance;
        }
        if (session.state() != AgentState::Unregistered) return RegistryResult::InvalidState;
        if (activeCount() >= capacity) return RegistryResult::CapacityReached;
        const auto id = session.id();
        (void)AgentStateMachine::beginConnecting(session);
        entries_.emplace(id, Entry{std::move(session), true});
        return RegistryResult::Registered;
    }

    [[nodiscard]] const Session* find(const SessionId& id) const noexcept
    {
        const auto it = entries_.find(id);
        return it == entries_.end() ? nullptr : &it->second.session;
    }

    [[nodiscard]] bool isActive(const SessionId& id) const noexcept
    {
        const auto it = entries_.find(id);
        return it != entries_.end() && it->second.enabled;
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    [[nodiscard]] std::size_t activeCount() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(),
            [](const auto& item) { return item.second.enabled; }));
    }

    [[nodiscard]] std::vector<const Session*> activeSessions() const
    {
        std::vector<const Session*> result;
        for (const auto& [id, entry] : entries_) {
            (void)id;
            if (entry.enabled) result.push_back(&entry.session);
        }
        std::sort(result.begin(), result.end(), [](const Session* a, const Session* b) {
            return a->id() < b->id();
        });
        return result;
    }

    [[nodiscard]] RegistryResult deactivate(const SessionId& id) noexcept
    {
        const auto it = entries_.find(id);
        if (it == entries_.end()) return RegistryResult::NotFound;
        auto& entry = it->second;
        if (!entry.enabled) return RegistryResult::AlreadyInactive;
        AgentStateMachine::deactivate(entry.session);
        entry.enabled = false;
        return RegistryResult::Deactivated;
    }

    [[nodiscard]] RegistryResult reconnect(const SessionId& id) noexcept
    {
        const auto it = entries_.find(id);
        if (it == entries_.end()) return RegistryResult::NotFound;
        auto& entry = it->second;
        if (!entry.enabled && activeCount() >= capacity) return RegistryResult::CapacityReached;
        if (AgentStateMachine::beginConnecting(entry.session) != TransitionResult::Applied)
            return RegistryResult::InvalidState;
        entry.enabled = true;
        return RegistryResult::Reconnecting;
    }

    // Transport loss reserves the registration slot and preserves replay history.
    void markDisconnected(const SessionId& id) noexcept
    {
        const auto it = entries_.find(id);
        if (it != entries_.end()) AgentStateMachine::deactivate(it->second.session);
    }

    [[nodiscard]] RegistryEventResult applyEvent(const AgentEvent& event)
    {
        const auto it = entries_.find(event.sessionId());
        if (it == entries_.end()) return RegistryResult::NotFound;
        if (!it->second.enabled) return RegistryResult::InactiveSession;
        return AgentStateMachine::apply(it->second.session, event);
    }

private:
    struct Entry {
        Session session;
        bool enabled;
    };
    std::unordered_map<SessionId, Entry> entries_;
};

}  // namespace dimepenny
