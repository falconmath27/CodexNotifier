#include "dimepenny/ipc/local_server.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QUuid>
#include <QtTest>
#include <functional>
#include <iostream>

namespace {
using namespace dimepenny::ipc;
int checks = 0;
void check(bool ok) { ++checks; if (!ok) throw std::runtime_error("Failed check " + std::to_string(checks)); }
bool wait(const std::function<bool()>& condition)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 2500) QTest::qWait(5);
    return condition();
}
Message msg(QString session, QString type, QJsonObject payload)
{
    return {1, QUuid::createUuid().toString(QUuid::WithoutBraces), std::move(session),
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), std::move(type), std::move(payload)};
}
Message registration(const QString& session, const QString& token)
{
    return msg(session, "register", {
        {"project", QJsonObject{{"name", "Test"}, {"directory", "C:\\Test"}}},
        {"pet", QJsonObject{{"id", "p-" + session}, {"name", "Penny"}, {"appearanceKey", session},
             {"color", QJsonObject{{"red", 1}, {"green", 2}, {"blue", 3}}}}},
        {"terminalType", "test"}, {"authToken", token}});
}
QString start(LocalServer& server)
{
    const auto endpoint = "dimepenny-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!server.listen(endpoint)) throw std::runtime_error("Listen failed: " + server.errorString().toStdString());
    check(true); return endpoint;
}
void connectPeer(QLocalSocket& socket, const QString& endpoint)
{
    socket.connectToServer(endpoint);
    check(wait([&] { return socket.state() == QLocalSocket::ConnectedState; }));
}
void login(QLocalSocket& socket, const QString& endpoint, const QString& session, const QString& token)
{
    connectPeer(socket, endpoint);
    socket.write(encodeFrame(registration(session, token)));
    check(wait([&] { return socket.bytesAvailable() > 0; }));
    FrameDecoder decoder;
    std::vector<Message> received;
    check(wait([&] {
        auto batch = decoder.feed(socket.read(maxReadBytes));
        received.insert(received.end(), batch.begin(), batch.end());
        return !received.empty();
    }));
    check(received.front().type == "registration_result" && received.front().payload.value("accepted").toBool());
}
bool disconnected(QLocalSocket& socket)
{
    return wait([&] { return socket.state() == QLocalSocket::UnconnectedState; });
}
void multipleClients()
{
    LocalServer server;
    const auto endpoint = start(server);
    int deliveries = 0;
    QObject::connect(&server, &LocalServer::messageReceived, &server, [&](const Message& message) {
        check(!message.payload.contains("authToken")); ++deliveries;
    });
    std::vector<std::unique_ptr<QLocalSocket>> sockets;
    QString firstToken;
    for (int i = 0; i < 10; ++i) {
        const auto id = "s" + QString::number(i);
        const auto token = server.authorize(id);
        if (i == 0) firstToken = token;
        auto socket = std::make_unique<QLocalSocket>();
        login(*socket, endpoint, id, token);
        sockets.push_back(std::move(socket));
    }
    check(server.connectionCount() == 10 && deliveries == 10);
    QLocalSocket excess; excess.connectToServer(endpoint);
    QTest::qWait(40); check(disconnected(excess)); check(server.connectionCount() == 10);
    sockets[0]->write(encodeFrame(msg("s1", "event", {{"eventType", "TaskStarted"}, {"sequence", "1"}})));
    check(disconnected(*sockets[0]));
    check(wait([&] { return server.connectionCount() == 9; }));
    check(deliveries == 10);
    login(*sockets[0], endpoint, "s0", firstToken);
    sockets[1]->abort();
    check(wait([&] { return server.connectionCount() == 9; }));
    QLocalSocket duplicate; connectPeer(duplicate, endpoint);
    duplicate.write(encodeFrame(registration("s0", firstToken)));
    check(disconnected(duplicate)); check(sockets[0]->state() == QLocalSocket::ConnectedState);
    sockets[0]->write(encodeFrame(msg("s0", "event", {{"eventType", "TaskStarted"}, {"sequence", "1"}})));
    check(wait([&] { return deliveries == 12; }));
    check(server.send(msg("s0", "approval_response", {{"requestId", "r1"}, {"decision", "deny"}})));
    check(wait([&] { return sockets[0]->bytesAvailable() > 0; }));
    FrameDecoder reply; check(reply.feed(sockets[0]->readAll()).front().type == "approval_response");
    server.revoke("s0"); check(disconnected(*sockets[0]));
    server.stop(); check(wait([&] { return server.connectionCount() == 0; }));
}
void authentication()
{
    LocalServer server;
    const auto endpoint = start(server);
    const auto token = server.authorize("s1");
    for (int mode = 0; mode < 4; ++mode) {
        QLocalSocket peer; connectPeer(peer, endpoint);
        auto message = registration("s1", token);
        if (mode == 0) message.payload.remove("authToken");
        if (mode == 1) message.payload.insert("authToken", QString(64, '0'));
        if (mode == 2) message.sessionId = "other";
        if (mode == 3) message = msg("s1", "heartbeat", {{"kind", "ping"}});
        peer.write(encodeFrame(message)); check(disconnected(peer));
    }
    QLocalSocket valid; login(valid, endpoint, "s1", token);
    const auto rotated = server.authorize("s1");
    check(rotated != token); check(disconnected(valid));
    QLocalSocket old; connectPeer(old, endpoint); old.write(encodeFrame(registration("s1", token)));
    check(disconnected(old));
    login(valid, endpoint, "s1", rotated);
    valid.write(encodeFrame(msg("s1", "approval_response", {{"requestId", "r"}, {"decision", "approve"}})));
    check(disconnected(valid));
}
void deadlines()
{
    LocalServer server{ServerOptions{120, 80, 150, 100, 5}};
    const auto endpoint = start(server);
    QLocalSocket silent; connectPeer(silent, endpoint); check(disconnected(silent));
    const auto token = server.authorize("s1");
    QString reason;
    QObject::connect(&server, &LocalServer::sessionDisconnected, &server,
                     [&](const QString&, const QString& why) { reason = why; });
    QLocalSocket peer; login(peer, endpoint, "s1", token);
    check(wait([&] { return peer.bytesAvailable() > 0; }));
    FrameDecoder decoder;
    const auto ping = decoder.feed(peer.readAll());
    check(ping.size() == 1 && ping.front().type == "heartbeat");
    peer.write(encodeFrame(msg("s1", "heartbeat", {{"kind", "pong"}, {"inReplyTo", ping.front().messageId}})));
    QTest::qWait(40); check(peer.state() == QLocalSocket::ConnectedState);
    check(disconnected(peer)); check(reason == "heartbeat_timeout");
    login(peer, endpoint, "s1", token);
    peer.write(encodeFrame(msg("s1", "heartbeat", {{"kind", "pong"}, {"inReplyTo", "wrong"}})));
    check(disconnected(peer)); check(reason == "heartbeat_mismatch");
    login(peer, endpoint, "s1", token);
    peer.write(QByteArray::fromHex("00000100"));
    check(disconnected(peer)); check(reason == "frame_timeout");
    login(peer, endpoint, "s1", token);
    peer.write(QByteArray::fromHex("ffffffff"));
    check(disconnected(peer)); check(reason == "invalid_protocol");
    login(peer, endpoint, "s1", token);
    peer.write(encodeFrame(msg("s1", "disconnect", {{"reason", "done"}})));
    check(disconnected(peer)); check(reason == "client_disconnect");
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try { multipleClients(); authentication(); deadlines();
        std::cout << checks << " local server checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
