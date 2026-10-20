#include "dimepenny/domain/agent_state_machine.h"

#include <array>
#include <iostream>
#include <limits>

namespace {
using namespace dimepenny;
using S = AgentState;
using E = AgentEventType;
using R = TransitionResult;
using M = AgentStateMachine;
int checks = 0;

void check(bool condition)
{
    ++checks;
    if (!condition) throw std::runtime_error("Failed check " + std::to_string(checks));
}

struct Fixture {
    Session session{SessionId{"session-1"}, ProjectInfo{"Project", std::filesystem::current_path()},
                    PetIdentity{"pet-1", "Penny", "round", {}}, "Terminal", time(100)};
    std::uint64_t serial = 0;

    static Session::Timestamp time(int seconds)
    {
        return Session::Timestamp{std::chrono::seconds{seconds}};
    }

    AgentEvent event(E type, std::string id = "candidate", std::uint64_t sequence = 100,
                     int seconds = 200, std::string request = "request-1",
                     ApprovalDecision decision = ApprovalDecision::Approve) const
    {
        std::optional<ApprovalInfo> approval;
        if (type == E::ApprovalRequested) approval = ApprovalInfo{std::move(request), {}};
        if (type == E::ApprovalResolved) approval = ApprovalInfo{std::move(request), decision};
        return AgentEvent{std::move(id), session.id(), type, time(seconds), sequence, approval};
    }

    void send(E type)
    {
        ++serial;
        check(M::apply(session, event(type, "setup-" + std::to_string(serial), serial,
                                    100 + static_cast<int>(serial))) == R::Applied);
    }

    void reach(S state)
    {
        if (state == S::Unregistered) return;
        check(M::beginConnecting(session) == R::Applied);
        if (state == S::Connecting) return;
        send(E::SessionConnected);
        if (state == S::Active) return;
        if (state == S::Error) { send(E::SessionError); return; }
        if (state == S::Disconnected) { send(E::SessionDisconnected); return; }
        send(E::TaskStarted);
        if (state == S::Working) return;
        send(state == S::Completed ? E::TaskCompleted : E::ApprovalRequested);
    }
};

void transitionMatrix()
{
    constexpr std::array states{S::Unregistered, S::Connecting, S::Active, S::Working,
                                S::ApprovalNeeded, S::Completed, S::Error, S::Disconnected};
    constexpr std::array events{E::SessionConnected, E::TaskStarted, E::TaskCompleted,
                                E::ApprovalRequested, E::ApprovalResolved,
                                E::SessionDisconnected, E::SessionError, E::TaskInterrupted};
    using Cell = std::optional<S>;
    constexpr Cell x = std::nullopt;
    const std::array<std::array<Cell, 8>, 8> expected{{
        {x, x, x, x, x, x, x, x},
        {S::Active, x, x, x, x, S::Disconnected, S::Error, x},
        {x, S::Working, x, x, x, S::Disconnected, S::Error, x},
        {x, x, S::Completed, S::ApprovalNeeded, x, S::Disconnected, S::Error, S::Active},
        {x, x, x, x, S::Working, S::Disconnected, S::Error, S::Active},
        {x, S::Working, x, x, x, S::Disconnected, S::Error, x},
        {x, x, x, x, x, S::Disconnected, x, x},
        {x, x, x, x, x, x, x, x}
    }};
    for (std::size_t row = 0; row < states.size(); ++row) {
        for (std::size_t col = 0; col < events.size(); ++col) {
            Fixture f;
            f.reach(states[row]);
            check(M::nextState(states[row], events[col]) == expected[row][col]);
            const auto result = M::apply(f.session, f.event(events[col]));
            check(result == (expected[row][col] ? R::Applied : R::InvalidTransition));
            check(f.session.state() == expected[row][col].value_or(states[row]));
        }
        Fixture f;
        f.reach(states[row]);
        const bool allowed = states[row] == S::Unregistered || states[row] == S::Disconnected
                             || states[row] == S::Error;
        check(M::beginConnecting(f.session) == (allowed ? R::Applied : R::InvalidTransition));
        check(f.session.state() == (allowed ? S::Connecting : states[row]));
    }
    check(!M::nextState(static_cast<S>(99), E::SessionConnected));
    check(!M::nextState(S::Working, static_cast<E>(99)));
}

void orderingAndIsolation()
{
    Fixture f;
    f.reach(S::Active); // accepted setup-1, sequence 1, timestamp 101
    check(M::apply(f.session, AgentEvent{"foreign", SessionId{"other"}, E::TaskStarted,
                                       Fixture::time(200), 100}) == R::WrongSession);
    check(M::apply(f.session, f.event(E::TaskStarted, "setup-1")) == R::DuplicateEvent);
    check(M::apply(f.session, f.event(E::TaskStarted, "candidate", 1)) == R::OutOfOrder);
    check(M::apply(f.session, f.event(E::TaskStarted, "candidate", 0)) == R::OutOfOrder);
    check(M::apply(f.session, f.event(E::TaskStarted, "candidate", 2, 100)) == R::OutOfOrder);
    check(M::apply(f.session, AgentEvent{"candidate", f.session.id(), E::TaskStarted,
                                       Fixture::time(200)}) == R::MissingSequence);
    check(M::apply(f.session, f.event(E::TaskCompleted, "candidate", 1000, 1000)) == R::InvalidTransition);
    check(f.session.state() == S::Active);
    // Failed messages above cannot reserve candidate's ID or advance the watermark.
    const auto start = f.event(E::TaskStarted, "candidate", 2, 101);
    check(M::apply(f.session, start) == R::Applied);
    check(M::apply(f.session, start) == R::DuplicateEvent);
    check(f.session.state() == S::Working);
    check(M::apply(f.session, f.event(E::TaskCompleted, "finish", 8, 102)) == R::Applied);
    check(M::apply(f.session, f.event(E::TaskStarted, "late", 7, 103)) == R::OutOfOrder);
    check(f.session.state() == S::Completed);

    Fixture unsequenced;
    check(M::beginConnecting(unsequenced.session) == R::Applied);
    check(M::apply(unsequenced.session, AgentEvent{"connect", unsequenced.session.id(),
          E::SessionConnected, Fixture::time(99)}) == R::OutOfOrder);
    check(M::apply(unsequenced.session, AgentEvent{"connect", unsequenced.session.id(),
          E::SessionConnected, Fixture::time(100)}) == R::Applied);
    check(M::apply(unsequenced.session, AgentEvent{"start", unsequenced.session.id(),
          E::TaskStarted, Fixture::time(99)}) == R::OutOfOrder);
    check(M::apply(unsequenced.session, AgentEvent{"start", unsequenced.session.id(),
          E::TaskStarted, Fixture::time(100)}) == R::Applied);
    // Sequence mode may start at zero but cannot subsequently be omitted.
    check(M::apply(unsequenced.session, unsequenced.event(E::TaskCompleted, "finish", 0)) == R::Applied);
    check(M::apply(unsequenced.session, AgentEvent{"next", unsequenced.session.id(),
          E::TaskStarted, Fixture::time(201)}) == R::MissingSequence);

    Fixture overflow;
    check(M::beginConnecting(overflow.session) == R::Applied);
    check(M::apply(overflow.session, overflow.event(E::SessionConnected, "max",
          std::numeric_limits<std::uint64_t>::max())) == R::Applied);
    check(M::apply(overflow.session, overflow.event(E::TaskStarted, "wrap", 0)) == R::OutOfOrder);
}

void approvalsAndReconnect()
{
    for (const auto decision : {ApprovalDecision::Approve, ApprovalDecision::Deny}) {
        Fixture f;
        f.reach(S::ApprovalNeeded);
        check(M::apply(f.session, f.event(E::ApprovalResolved, "resolve", 100, 200, "other"))
              == R::ApprovalMismatch);
        check(f.session.state() == S::ApprovalNeeded);
        check(M::apply(f.session, f.event(E::ApprovalResolved, "resolve", 100, 200,
                                         "request-1", decision)) == R::Applied);
        check(f.session.state() == S::Working);
        check(M::apply(f.session, f.event(E::ApprovalResolved, "again", 101, 201)) == R::InvalidTransition);
    }
    for (const auto interruption : {E::SessionDisconnected, E::SessionError}) {
        Fixture f;
        f.reach(S::ApprovalNeeded);
        check(M::apply(f.session, f.event(interruption, "interrupt", 100)) == R::Applied);
        check(M::beginConnecting(f.session) == R::Applied);
        check(M::apply(f.session, f.event(E::SessionConnected, "reconnect", 0, 201)) == R::OutOfOrder);
        check(M::apply(f.session, f.event(E::SessionConnected, "reconnect", 101, 201)) == R::Applied);
        check(M::apply(f.session, f.event(E::TaskStarted, "new-task", 102, 202)) == R::Applied);
        check(M::apply(f.session, f.event(E::ApprovalResolved, "old-resolution", 103, 203)) == R::InvalidTransition);
        check(M::apply(f.session, f.event(E::ApprovalRequested, "new-request", 103, 203, "request-2")) == R::Applied);
        check(M::apply(f.session, f.event(E::ApprovalResolved, "old-resolution", 104, 204)) == R::ApprovalMismatch);
        check(M::apply(f.session, f.event(E::ApprovalResolved, "new-resolution", 104, 204, "request-2")) == R::Applied);
        check(M::apply(f.session, f.event(E::TaskCompleted, "finish", 105, 205)) == R::Applied);
        check(f.session.state() == S::Completed);
    }
}
}  // namespace

int main()
{
    try {
        transitionMatrix();
        orderingAndIsolation();
        approvalsAndReconnect();
        std::cout << checks << " state machine checks passed (all 56 state/event pairs)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
