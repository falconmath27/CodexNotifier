#include "dimepenny/ipc/simulated_bridge.h"
#include "dimepenny/domain/agent_state_machine.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QUuid>
#include <QtTest>
#include <functional>
#include <iostream>
#include <source_location>

namespace {
using namespace dimepenny;
using namespace dimepenny::ipc;
int checks = 0;
void check(bool value, const std::source_location location = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error("Failed check " + std::to_string(checks)
        + " at line " + std::to_string(location.line()));
}
bool wait(const std::function<bool()>& condition)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 2500) QTest::qWait(5);
    return condition();
}
QJsonObject payload(const QString& id)
{
    return {{"project", QJsonObject{{"name", "Fake"}, {"directory", "C:\\Fake"}}},
        {"pet", QJsonObject{{"id", id}, {"name", "Penny"}, {"appearanceKey", id},
            {"color", QJsonObject{{"red", 0}, {"green", 0}, {"blue", 0}}}}},
        {"terminalType", "simulation"}, {"codexSessionId", "fake-" + id}};
}
void exercise()
{
    LocalServer server{ServerOptions{500, 30, 150, 100, 5}};
    const auto endpoint = "dimepenny-bridge-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!server.listen(endpoint)) throw std::runtime_error(server.errorString().toStdString());
    std::vector<Message> received;
    QString disconnectReason;
    QObject::connect(&server, &LocalServer::messageReceived, &server, [&](const Message& message) {
        check(!message.payload.contains("authToken"));
        if (message.type == "event") received.push_back(message);
    });
    QObject::connect(&server, &LocalServer::sessionDisconnected, &server,
                     [&](const QString&, const QString& reason) { disconnectReason = reason; });
    SimulatedBridge a{endpoint, "a", server.authorize("a"), payload("a")};
    SimulatedBridge b{endpoint, "b", server.authorize("b"), payload("b")};
    check(!a.sendEvent("TaskStarted") && !a.replayLastEvent() && !a.sendMalformedFrame());
    a.connectToServer(); b.connectToServer();
    check(wait([&] { return a.isReady() && b.isReady(); }));
    check(server.connectionCount() == 2);
    QTest::qWait(350); // Several heartbeat cycles; both clients must answer.
    check(a.isReady() && b.isReady() && server.connectionCount() == 2);
    check(a.sendEvent("SessionConnected"));
    check(a.sendEvent("TaskStarted"));
    check(a.sendEvent("TaskCompleted"));
    check(a.replayLastEvent());
    check(b.sendEvent("SessionConnected"));
    check(wait([&] { return received.size() == 5; }));
    std::vector<Message> aEvents;
    for (const auto& event : received) if (event.sessionId == "a") aEvents.push_back(event);
    check(aEvents.size() == 4);
    check(encodeJson(aEvents[2]) == encodeJson(aEvents[3]));
    // Test-only mapping demonstrates that the exact replay is rejected by Task 3.
    Session model{SessionId{"a"}, ProjectInfo{"Fake", std::filesystem::current_path()},
        PetIdentity{"a", "Penny", "a", {}}, "simulation", Session::Timestamp{}};
    check(AgentStateMachine::beginConnecting(model) == TransitionResult::Applied);
    const std::array types{AgentEventType::SessionConnected, AgentEventType::TaskStarted,
                          AgentEventType::TaskCompleted, AgentEventType::TaskCompleted};
    for (std::size_t i = 0; i < aEvents.size(); ++i) {
        const auto& m = aEvents[i];
        const auto timestamp = Session::Timestamp{std::chrono::duration_cast<Session::Timestamp::duration>(
            std::chrono::milliseconds{QDateTime::fromString(m.timestamp, Qt::ISODateWithMs).toMSecsSinceEpoch()})};
        AgentEvent event{m.messageId.toStdString(), SessionId{"a"}, types[i], timestamp,
                        m.payload.value("sequence").toString().toULongLong()};
        check(AgentStateMachine::apply(model, event) == (i == 3 ? TransitionResult::DuplicateEvent : TransitionResult::Applied));
    }
    check(model.state() == AgentState::Completed);
    a.disconnectFromServer();
    check(wait([&] { return server.connectionCount() == 1; }));
    a.connectToServer(); check(wait([&] { return a.isReady(); }));
    check(a.sendEvent("SessionConnected"));
    check(wait([&] { return received.size() == 6; }));
    check(received.back().payload.value("sequence") == "3");
    check(a.sendMalformedFrame());
    check(wait([&] { return !a.isReady() && server.connectionCount() == 1; }));
    check(disconnectReason == "invalid_protocol" && b.isReady());
    a.connectToServer(); check(wait([&] { return a.isReady(); }));
    check(a.sendEvent("SessionConnected"));
    check(wait([&] { return received.size() == 7; }));
    check(received.back().payload.value("sequence") == "4");
    bool reply = false;
    QObject::connect(&a, &SimulatedBridge::messageReceived, &a, [&](const Message& m) { reply = m.type == "approval_response"; });
    check(server.send({1, "reply", "a", "2026-10-19T00:00:00.000Z", "approval_response",
                       {{"requestId", "r"}, {"decision", "deny"}}}));
    check(wait([&] { return reply; }));
    SimulatedBridge bad{endpoint, "unknown", QString(64, '0'), payload("bad")};
    bool rejected = false;
    QObject::connect(&bad, &SimulatedBridge::failed, &bad, [&](const QString&) { rejected = true; });
    bad.connectToServer(); check(wait([&] { return rejected; })); check(!bad.isReady());
    server.stop();
    check(wait([&] { return !a.isReady() && !b.isReady(); }));
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try { exercise(); std::cout << checks << " simulated bridge checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
