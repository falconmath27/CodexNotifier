#include "dimepenny/application/event_processor.h"
#include "dimepenny/ipc/simulated_bridge.h"
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
using R = ProcessingResult;
int checks = 0;
void check(bool value, std::source_location where = std::source_location::current())
{
    ++checks;
    if (!value) throw std::runtime_error("Check failed at line " + std::to_string(where.line()));
}
bool wait(const std::function<bool()>& condition)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 2500) QTest::qWait(5);
    return condition();
}
QJsonObject registration(const QString& id)
{
    return {{"project", QJsonObject{{"name", "Fake"}, {"directory", "C:\\Fake"}}},
        {"pet", QJsonObject{{"id", id}, {"name", "Penny"}, {"appearanceKey", id},
            {"color", QJsonObject{{"red", 0}, {"green", 0}, {"blue", 0}}}}},
        {"terminalType", "test"}, {"codexSessionId", "fake-" + id}};
}
Message event(const QString& id, const QString& type, int sequence, QJsonObject approval = {})
{
    QJsonObject payload{{"eventType", type}, {"sequence", QString::number(sequence)}};
    if (!approval.isEmpty()) payload.insert("approval", approval);
    return {1, "e-" + QString::number(sequence), id,
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), "event", payload};
}
void exercise()
{
    EventProcessor processor;
    auto& server = processor.server();
    const auto endpoint = "processor-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    check(server.listen(endpoint));
    const auto tokenA = server.authorize("a");
    SimulatedBridge a{endpoint, "a", tokenA, registration("a")};
    SimulatedBridge b{endpoint, "b", server.authorize("b"), registration("b")};
    check(processor.process("a", event("a", "SessionConnected", 0)) == R::NotAuthenticated);
    a.connectToServer(); b.connectToServer();
    check(wait([&] { return a.isReady() && b.isReady(); }));
    auto state = [&](const char* id) { return processor.registry().find(SessionId{id})->state(); };
    check(state("a") == AgentState::Connecting && state("b") == AgentState::Connecting);
    int updates = 0;
    QObject::connect(&processor, &EventProcessor::stateChanged, &processor, [&](const QString&) { ++updates; });
    const auto connectedA = event("a", "SessionConnected", 0);
    check(processor.process("a", connectedA) == R::Applied);
    check(processor.process("a", connectedA) == R::Duplicate);
    check(updates == 1 && state("a") == AgentState::Active);
    check(processor.process("a", event("b", "SessionConnected", 0)) == R::WrongSession);
    check(state("b") == AgentState::Connecting);
    check(processor.process("b", event("b", "SessionConnected", 0)) == R::Applied); // same ID scoped per session
    check(processor.process("a", event("a", "TaskCompleted", 1)) == R::InvalidTransition);
    auto malformed = event("a", "TaskStarted", 1);
    malformed.payload.insert("sequence", 1);
    check(processor.process("a", malformed) == R::InvalidMessage);
    auto invalidVersion = event("a", "TaskStarted", 1); invalidVersion.version = 2;
    check(processor.process("a", invalidVersion) == R::InvalidMessage);
    auto invalidDate = event("a", "TaskStarted", 1); invalidDate.timestamp = "9999-01-01T00:00:00.000Z";
    check(processor.process("a", invalidDate) == R::InvalidMessage);
    check(state("a") == AgentState::Active && state("b") == AgentState::Active);
    check(processor.process("a", event("a", "TaskStarted", 1)) == R::Applied); // rejected ID not consumed
    auto stale = event("a", "TaskCompleted", 2); stale.timestamp = "1970-01-01T00:00:00.000Z";
    check(processor.process("a", stale) == R::OutOfOrder);
    auto staleSequence = event("a", "TaskCompleted", 1); staleSequence.messageId = "different-id";
    check(processor.process("a", staleSequence) == R::OutOfOrder);
    check(processor.process("a", event("a", "ApprovalRequested", 2, {{"requestId", "request-a"}})) == R::Applied);
    check(processor.process("a", event("a", "ApprovalResolved", 3,
        {{"requestId", "request-b"}, {"decision", "approve"}})) == R::ApprovalMismatch);
    check(state("a") == AgentState::ApprovalNeeded && state("b") == AgentState::Active);
    check(processor.process("a", event("a", "ApprovalResolved", 3,
        {{"requestId", "request-a"}, {"decision", "deny"}})) == R::Applied);
    check(processor.process("a", event("a", "TaskCompleted", 4)) == R::Applied);
    check(state("a") == AgentState::Completed);
    auto diagnostic = event("a", "SessionError", 5);
    diagnostic.type = "error"; diagnostic.payload = {{"code", "test"}, {"message", "diagnostic only"}};
    check(processor.process("a", diagnostic) == R::Ignored);
    check(state("a") == AgentState::Completed);
    check(processor.process("a", event("a", "SessionError", 5)) == R::Applied);
    check(processor.process("a", event("a", "SessionDisconnected", 6)) == R::Applied);
    check(state("b") == AgentState::Active);
    const auto pet = processor.registry().find(SessionId{"a"})->pet();
    a.disconnectFromServer();
    check(wait([&] { return server.connectionCount() == 1; }));
    check(processor.process("a", event("a", "SessionConnected", 7)) == R::NotAuthenticated);
    check(processor.registry().activeCount() == 2); // disconnect reserves registration
    a.connectToServer(); check(wait([&] { return a.isReady(); }));
    check(processor.registry().find(SessionId{"a"})->pet() == pet);
    check(processor.process("a", connectedA) == R::Duplicate);
    check(state("a") == AgentState::Connecting);
    check(processor.process("a", event("a", "SessionConnected", 7)) == R::Applied);

    // Real bridge-generated events through authentication, codec, dispatcher and state machine.
    SimulatedBridge c{endpoint, "c", server.authorize("c"), registration("c")};
    c.connectToServer(); check(wait([&] { return c.isReady(); }));
    check(c.sendEvent("SessionConnected") && c.sendEvent("TaskStarted") && c.sendEvent("TaskCompleted"));
    check(wait([&] { return state("c") == AgentState::Completed; }));
    int duplicates = 0;
    QObject::connect(&processor, &EventProcessor::eventProcessed, &processor,
        [&](const QString&, const QString&, R result) { if (result == R::Duplicate) ++duplicates; });
    check(c.replayLastEvent()); check(wait([&] { return duplicates == 1; }));
    check(state("c") == AgentState::Completed);
    check(c.sendMalformedFrame());
    check(wait([&] { return state("c") == AgentState::Disconnected; }));
    check(state("a") == AgentState::Active && state("b") == AgentState::Active);
    c.connectToServer(); check(wait([&] { return c.isReady(); }));
    check(c.sendEvent("SessionConnected")); check(wait([&] { return state("c") == AgentState::Active; }));

    // Domain collision is rejected before transport claims acceptance.
    auto collision = registration("d"); collision.insert("pet", registration("a").value("pet"));
    SimulatedBridge d{endpoint, "d", server.authorize("d"), collision};
    bool rejected = false;
    QObject::connect(&d, &SimulatedBridge::failed, &d, [&](const QString&) { rejected = true; });
    d.connectToServer(); check(wait([&] { return rejected; }));
    check(!d.isReady() && !processor.registry().find(SessionId{"d"}));
    server.stop();
    check(state("a") == AgentState::Disconnected && state("b") == AgentState::Disconnected);
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try { exercise(); std::cout << checks << " event processing checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
