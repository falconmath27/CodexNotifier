#include "dimepenny/ipc/simulated_bridge.h"
#include "dimepenny/application/event_processor.h"

#include <QCoreApplication>
#include <QDebug>
#include <QCommandLineParser>
#include <QDir>
#include <QUuid>

int main(int argc, char* argv[])
{
    using namespace dimepenny::ipc;
    QCoreApplication app(argc, argv);
    app.setApplicationName("DimePennyBridge");
    QCommandLineParser parser;
    parser.setApplicationDescription("Fake Codex session simulator; no real Codex integration");
    parser.addHelpOption();
    parser.addOption({"demo", "Run with an embedded local test server."});
    parser.addOption({"endpoint", "Existing Dime&Penny endpoint.", "name"});
    parser.addOption({"session", "Provisioned session identity.", "id", "simulated-1"});
    parser.addOption({"scenario", "normal, duplicate, reconnect, or malformed", "name", "normal"});
    parser.process(app);
    const auto scenario = parser.value("scenario");
    if (!QStringList{"normal", "duplicate", "reconnect", "malformed"}.contains(scenario)
        || (parser.isSet("demo") == parser.isSet("endpoint"))) {
        qCritical() << "Choose --demo or --endpoint NAME and a valid --scenario."; return 2;
    }
    try {
        dimepenny::EventProcessor processor;
        auto& demo = processor.server();
        QObject::connect(&processor, &dimepenny::EventProcessor::stateChanged, &app, [&](const QString& sessionId) {
            const auto* session = processor.registry().find(dimepenny::SessionId{sessionId.toStdString()});
            if (session) qInfo().noquote() << sessionId << QString::fromLatin1(dimepenny::toString(session->state()).data());
        });
        QString endpoint = parser.value("endpoint");
        const auto id = parser.value("session");
        QString token = qEnvironmentVariable("DIMEPENNY_AUTH_TOKEN");
        if (parser.isSet("demo")) {
            endpoint = "dimepenny-demo-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            if (!demo.listen(endpoint)) { qCritical() << demo.errorString(); return 1; }
            token = demo.authorize(id);
            QObject::connect(&demo, &LocalServer::messageReceived, &app, [](const Message& message) {
                qInfo().noquote() << "Received" << message.type << message.payload.value("eventType").toString()
                                 << "sequence" << message.payload.value("sequence").toString();
            });
        }
        if (token.isEmpty()) { qCritical() << "Set the server-provisioned DIMEPENNY_AUTH_TOKEN."; return 2; }
        QJsonObject registration{
            {"project", QJsonObject{{"name", "Simulated project"}, {"directory", QDir::toNativeSeparators(QDir::currentPath())}}},
            {"pet", QJsonObject{{"id", "pet-" + id}, {"name", "Simulated Penny"}, {"appearanceKey", "simulated-" + id},
                {"color", QJsonObject{{"red", 240}, {"green", 100}, {"blue", 25}}}}},
            {"terminalType", "simulator"}, {"codexSessionId", "fake-" + id}};
        SimulatedBridge bridge{endpoint, id, token, registration};
        int connections = 0;
        bool expectedClose = false;
        bool reconnecting = false;
        QObject::connect(&bridge, &SimulatedBridge::registered, &app, [&] {
            ++connections;
            qInfo() << "Fake session registered; connection" << connections;
            if (!bridge.sendEvent("SessionConnected") || !bridge.sendEvent("TaskStarted")
                || !bridge.sendEvent("TaskCompleted")) { app.exit(1); return; }
            if (scenario == "duplicate" && !bridge.replayLastEvent()) { app.exit(1); return; }
            QTimer::singleShot(150, &bridge, [&] {
                expectedClose = true;
                reconnecting = scenario == "reconnect" && connections == 1;
                if (scenario == "malformed") { if (!bridge.sendMalformedFrame()) app.exit(1); }
                else bridge.disconnectFromServer();
            });
        });
        QObject::connect(&bridge, &SimulatedBridge::disconnected, &app, [&] {
            if (reconnecting) {
                reconnecting = false; expectedClose = false;
                QTimer::singleShot(100, &bridge, &SimulatedBridge::connectToServer);
            } else app.exit(expectedClose ? 0 : 1);
        });
        QObject::connect(&bridge, &SimulatedBridge::failed, &app, [&](const QString& reason) {
            if (!expectedClose) { qCritical() << reason; app.exit(1); }
        });
        QTimer::singleShot(10000, &app, [&] { qCritical() << "Simulation timed out"; app.exit(1); });
        bridge.connectToServer();
        return app.exec();
    } catch (const std::exception&) {
        qCritical() << "Invalid simulation configuration."; return 2;
    }
}

