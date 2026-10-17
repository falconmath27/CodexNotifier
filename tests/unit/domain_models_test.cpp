#include "dimepenny/domain/agent_event.h"
#include "dimepenny/domain/session.h"

#include <array>
#include <iostream>
#include <unordered_map>

namespace {
int checks = 0;

void check(bool condition)
{
    ++checks;
    if (!condition) throw std::runtime_error("Failed check " + std::to_string(checks));
}

template <typename Function>
void rejects(Function action)
{
    try {
        action();
    } catch (const std::invalid_argument&) {
        check(true);
        return;
    }
    check(false);
}
}  // namespace

int main()
{
    using namespace dimepenny;
    try {
        const SessionId id{"session-1"};
        check(id == SessionId{"session-1"});
        check(id != SessionId{"session-2"});
        std::unordered_map<SessionId, int> sessions{{id, 42}};
        check(sessions.at(SessionId{"session-1"}) == 42);
        for (const auto* invalid : {"", " ", "two words", "tab\tid", "id/path", "\nid"}) {
            rejects([&] { SessionId bad{invalid}; });
        }
        rejects([] { SessionId bad{std::string(129, 'a')}; });
        check(SessionId{std::string(128, 'a')}.value().size() == 128);

        const auto directory = std::filesystem::current_path() / "missing-project";
        const ProjectInfo project{"Sample project", directory};
        check(project.directory() == directory);
        check(project.name() == "Sample project");
        rejects([&] { ProjectInfo bad{"", directory}; });
        rejects([] { ProjectInfo bad{"Project", "relative"}; });
        rejects([] { ProjectInfo bad{"Project", {}}; });

        const PetIdentity pet{"pet-1", "Penny", "round-striped", {240, 100, 25}};
        check(pet.id() == "pet-1" && pet.name() == "Penny");
        check(pet.appearanceKey() == "round-striped");
        check(pet.color() == PetColor{240, 100, 25});
        rejects([] { PetIdentity bad{"", "Penny", "round", {}}; });
        rejects([] { PetIdentity bad{"p", "\t", "round", {}}; });
        rejects([] { PetIdentity bad{"p", "Penny", "", {}}; });

        const auto now = Session::Timestamp{std::chrono::seconds{1234}};
        const Session session{id, project, pet, "Windows Terminal", now};
        check(session.id() == id && session.project() == project && session.pet() == pet);
        check(session.createdAt() == now && session.terminalType() == "Windows Terminal");
        check(session.state() == AgentState::Unregistered);
        check(!session.processId() && !session.codexSessionId());
        const Session identified{id, project, pet, "Terminal", now, "codex-1", 123};
        check(identified.codexSessionId() == "codex-1" && identified.processId() == 123);
        rejects([&] { Session bad{id, project, pet, "", now}; });
        rejects([&] { Session bad{id, project, pet, "Terminal", now, ""}; });
        rejects([&] { Session bad{id, project, pet, "Terminal", now, std::nullopt, 0}; });

        constexpr std::array states{
            AgentState::Unregistered, AgentState::Connecting, AgentState::Active,
            AgentState::Working, AgentState::ApprovalNeeded, AgentState::Completed,
            AgentState::Error, AgentState::Disconnected};
        for (const auto state : states) check(toString(state) != "Unknown");

        constexpr std::array ordinaryEvents{
            AgentEventType::SessionConnected, AgentEventType::TaskStarted,
            AgentEventType::TaskCompleted, AgentEventType::SessionDisconnected,
            AgentEventType::SessionError};
        for (const auto type : ordinaryEvents) {
            const AgentEvent event{"event-1", id, type, now, 0};
            check(event.eventId() == "event-1" && event.sessionId() == id);
            check(event.type() == type && event.occurredAt() == now);
            check(event.sequence() == 0 && !event.approval());
            rejects([&] { AgentEvent bad{"e", id, type, now, {}, ApprovalInfo{"r", {}}}; });
        }
        const AgentEvent request{"request-event", id, AgentEventType::ApprovalRequested,
                                 now, {}, ApprovalInfo{"request-1", {}}};
        check(request.approval()->requestId == "request-1" && !request.sequence());
        for (const auto decision : {ApprovalDecision::Approve, ApprovalDecision::Deny}) {
            const AgentEvent response{"response", id, AgentEventType::ApprovalResolved,
                                      now, 2, ApprovalInfo{"request-1", decision}};
            check(response.approval()->decision == decision);
        }
        rejects([&] { AgentEvent bad{"", id, AgentEventType::TaskStarted, now}; });
        rejects([&] { AgentEvent bad{"e", id, AgentEventType::ApprovalRequested, now}; });
        rejects([&] { AgentEvent bad{"e", id, AgentEventType::ApprovalResolved, now}; });
        rejects([&] { AgentEvent bad{"e", id, AgentEventType::ApprovalResolved, now,
                                    {}, ApprovalInfo{"r", {}}}; });
        rejects([&] { AgentEvent bad{"e", id, AgentEventType::ApprovalRequested, now,
                                    {}, ApprovalInfo{"r", ApprovalDecision::Approve}}; });
        rejects([&] { AgentEvent bad{"e", id, AgentEventType::ApprovalRequested, now,
                                    {}, ApprovalInfo{"", {}}}; });
        rejects([&] { AgentEvent bad{"e", id, static_cast<AgentEventType>(99), now}; });
        rejects([&] { AgentEvent bad{"e", id, AgentEventType::ApprovalResolved, now,
                                    {}, ApprovalInfo{"r", static_cast<ApprovalDecision>(99)}}; });
        std::cout << checks << " domain checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
