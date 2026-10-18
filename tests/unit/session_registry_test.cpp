#include "dimepenny/domain/session_registry.h"
#include <iostream>

namespace {
using namespace dimepenny;
using R = RegistryResult;
using T = TransitionResult;
using E = AgentEventType;
int checks = 0;
void check(bool condition)
{
    ++checks;
    if (!condition) throw std::runtime_error("Failed check " + std::to_string(checks));
}
Session make(int number, std::string petId = {}, std::string appearance = {},
             std::optional<std::string> codexId = {})
{
    const auto suffix = std::to_string(number);
    return Session{SessionId{"s" + suffix}, ProjectInfo{"Project", std::filesystem::current_path()},
        PetIdentity{petId.empty() ? "p" + suffix : petId, "Penny",
                    appearance.empty() ? "style" + suffix : appearance, {}},
        "Terminal", Session::Timestamp{}, codexId, 123};
}
AgentEvent event(int session, E type, int sequence, std::optional<ApprovalInfo> approval = {})
{
    return AgentEvent{"e" + std::to_string(sequence), SessionId{"s" + std::to_string(session)},
        type, Session::Timestamp{std::chrono::seconds{sequence}},
        static_cast<std::uint64_t>(sequence), std::move(approval)};
}
void identityChecks()
{
    SessionRegistry registry;
    check(!registry.find(SessionId{"missing"}));
    check(registry.activeSessions().empty());
    check(registry.deactivate(SessionId{"missing"}) == R::NotFound);
    check(registry.reconnect(SessionId{"missing"}) == R::NotFound);
    check(registry.applyEvent(event(99, E::SessionConnected, 1)) == RegistryEventResult{R::NotFound});
    check(registry.registerSession(make(1, {}, {}, "codex-1")) == R::Registered);
    check(registry.registerSession(make(1)) == R::DuplicateSessionId);
    check(registry.registerSession(make(2, {}, {}, "codex-1")) == R::DuplicateCodexId);
    check(registry.registerSession(make(2, "p1")) == R::DuplicatePetId);
    check(registry.registerSession(make(2, {}, "style1")) == R::DuplicateAppearance);
    check(registry.size() == 1 && registry.activeCount() == 1);
    const auto* session = registry.find(SessionId{"s1"});
    check(session && session->state() == AgentState::Connecting);
    check(session->pet() == make(1).pet());
    check(registry.reconnect(session->id()) == R::InvalidState);
    auto alreadyStarted = make(3);
    (void)AgentStateMachine::beginConnecting(alreadyStarted);
    check(registry.registerSession(alreadyStarted) == R::InvalidState);
    check(registry.registerSession(make(2)) == R::Registered); // same PID, project and name allowed
    check(registry.applyEvent(event(1, E::SessionConnected, 1)) == RegistryEventResult{T::Applied});
    check(registry.find(SessionId{"s2"})->state() == AgentState::Connecting);
    check(registry.deactivate(SessionId{"s1"}) == R::Deactivated);
    check(registry.deactivate(SessionId{"s1"}) == R::AlreadyInactive);
    check(registry.find(SessionId{"s1"})->state() == AgentState::Disconnected);
    check(registry.registerSession(make(3, "p1")) == R::DuplicatePetId);
    check(registry.registerSession(make(3, {}, {}, "codex-1")) == R::DuplicateCodexId);
    check(registry.applyEvent(event(1, E::TaskStarted, 2)) == RegistryEventResult{R::InactiveSession});
    check(registry.reconnect(SessionId{"s1"}) == R::Reconnecting);
    check(registry.find(SessionId{"s1"}) == session); // stable view across insertions and reconnect
    check(session->pet() == make(1).pet());
    check(registry.applyEvent(event(1, E::SessionConnected, 1)) == RegistryEventResult{T::DuplicateEvent});
    check(registry.applyEvent(event(1, E::SessionConnected, 2)) == RegistryEventResult{T::Applied});
}
void capacityChecks()
{
    SessionRegistry registry;
    for (int i = 0; i < 10; ++i) check(registry.registerSession(make(i)) == R::Registered);
    check(registry.activeCount() == 10 && registry.activeSessions().size() == 10);
    check(registry.registerSession(make(10)) == R::CapacityReached);
    check(!registry.find(SessionId{"s10"}));
    check(registry.deactivate(SessionId{"s0"}) == R::Deactivated);
    check(registry.activeCount() == 9 && registry.size() == 10);
    check(registry.registerSession(make(10)) == R::Registered);
    check(registry.reconnect(SessionId{"s0"}) == R::CapacityReached);
    check(!registry.isActive(SessionId{"s0"}));
    check(registry.find(SessionId{"s0"})->state() == AgentState::Disconnected);
    check(registry.deactivate(SessionId{"s10"}) == R::Deactivated);
    check(registry.reconnect(SessionId{"s0"}) == R::Reconnecting);
    check(registry.find(SessionId{"s0"})->pet() == make(0).pet());
    check(registry.applyEvent(event(0, E::SessionDisconnected, 1)) == RegistryEventResult{T::Applied});
    check(registry.activeCount() == 10); // transient disconnect reserves its slot
    check(registry.reconnect(SessionId{"s0"}) == R::Reconnecting); // needs no extra slot
    check(registry.activeCount() == 10);
    const auto active = registry.activeSessions();
    check(std::is_sorted(active.begin(), active.end(), [](const Session* a, const Session* b) {
        return a->id() < b->id();
    }));
}
void lifecycleChecks()
{
    SessionRegistry registry;
    check(registry.registerSession(make(1)) == R::Registered);
    check(registry.applyEvent(event(1, E::SessionConnected, 1)) == RegistryEventResult{T::Applied});
    check(registry.applyEvent(event(1, E::TaskStarted, 2)) == RegistryEventResult{T::Applied});
    check(registry.applyEvent(event(1, E::ApprovalRequested, 3, ApprovalInfo{"r1", {}})) == RegistryEventResult{T::Applied});
    check(registry.deactivate(SessionId{"s1"}) == R::Deactivated);
    check(registry.reconnect(SessionId{"s1"}) == R::Reconnecting);
    check(registry.applyEvent(event(1, E::SessionConnected, 4)) == RegistryEventResult{T::Applied});
    check(registry.applyEvent(event(1, E::ApprovalResolved, 5, ApprovalInfo{"r1", ApprovalDecision::Approve}))
          == RegistryEventResult{T::InvalidTransition});
    check(registry.applyEvent(event(1, E::TaskStarted, 5)) == RegistryEventResult{T::Applied});
    check(registry.applyEvent(event(1, E::TaskCompleted, 6)) == RegistryEventResult{T::Applied});
    check(registry.applyEvent(event(1, E::SessionError, 7)) == RegistryEventResult{T::Applied});
    check(registry.reconnect(SessionId{"s1"}) == R::Reconnecting);
    check(registry.find(SessionId{"s1"})->pet() == make(1).pet());
}
}
int main()
{
    try {
        identityChecks(); capacityChecks(); lifecycleChecks();
        std::cout << checks << " registry checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
