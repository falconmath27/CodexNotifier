#include "dimepenny/integration/interactive_server.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>

namespace dimepenny {
namespace {
constexpr qsizetype limit = 65536;
bool identifier(const QString& value) { return !value.trimmed().isEmpty() && value.size() <= 256; }
}
InteractiveServer::InteractiveServer(Register registration, QObject* parent)
    : QObject(parent), registration_(std::move(registration))
{
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    server_.setMaxPendingConnections(32);
    connect(&server_, &QLocalServer::newConnection, this, &InteractiveServer::accept);
}
InteractiveServer::~InteractiveServer() { stop(); }
QString InteractiveServer::defaultEndpoint()
{
    return "dimepenny-interactive-v1-" + QString::fromLatin1(QCryptographicHash::hash(
        QDir::homePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
}
bool InteractiveServer::listen(const QString& endpoint) { return server_.listen(endpoint); }
void InteractiveServer::stop()
{
    server_.close();
    const auto clients = clients_.values();
    for (const auto& client : clients) {
        release(client);
        if (client->socket) client->socket->abort();
    }
}
void InteractiveServer::release(const std::shared_ptr<Client>& client)
{
    client->deadline.stop();
    tokens_.remove(client->token);
    if (client->bridge) {
        auto* bridge = client->bridge.data();
        client->bridge.clear();
        bridge->disconnect(this);
        bridge->disconnectFromServer();
        bridge->deleteLater();
    }
}
void InteractiveServer::accept()
{
    while (auto* socket = server_.nextPendingConnection()) {
        if (clients_.size() >= 32) { socket->abort(); socket->deleteLater(); continue; }
        auto client = std::make_shared<Client>();
        client->socket = socket;
        socket->setReadBufferSize(limit + 1);
        client->deadline.setSingleShot(true);
        clients_.insert(socket, client);
        connect(&client->deadline, &QTimer::timeout, socket, [socket] { socket->abort(); });
        client->deadline.start(5000);
        connect(socket, &QLocalSocket::readyRead, this, [this, client] { read(client); });
        connect(socket, &QLocalSocket::disconnected, this, [this, client, socket] {
            release(client); clients_.remove(socket); socket->deleteLater();
        });
        if (socket->bytesAvailable()) read(client);
    }
}
void InteractiveServer::reply(Client& client, QJsonObject message, bool close)
{
    if (!client.socket) return;
    message.insert("v", 1);
    const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (client.socket->bytesToWrite() > limit || client.socket->write(bytes) != bytes.size()) {
        client.socket->abort(); return;
    }
    if (close) client.socket->disconnectFromServer();
}
void InteractiveServer::read(const std::shared_ptr<Client>& client)
{
    client->buffer += client->socket->read(limit + 1 - client->buffer.size());
    if (client->buffer.size() > limit) { client->socket->abort(); return; }
    while (client->socket && client->socket->state() == QLocalSocket::ConnectedState) {
        const auto end = client->buffer.indexOf('\n');
        if (end < 0) break;
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(client->buffer.left(end), &error);
        client->buffer.remove(0, end + 1);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            reply(*client, {{"ok", false}, {"error", "Invalid JSON"}}, true); return;
        }
        request(client, doc.object());
    }
}
void InteractiveServer::request(const std::shared_ptr<Client>& client, const QJsonObject& message)
{
    const auto reject = [this, &client](const QString& reason) {
        reply(*client, {{"ok", false}, {"error", reason}}, true);
    };
    if (message.value("v") != 1) { reject("Unsupported version"); return; }
    const auto op = message.value("op").toString();
    if (client->requested) {
        if (!client->registered || op != "ping") { reject("Expected lease heartbeat"); return; }
        client->deadline.start(10000);
        reply(*client, {{"ok", true}}); return;
    }
    client->requested = true;
    if (op == "hook") {
        const auto token = message.value("token").toString();
        auto owner = tokens_.value(token).lock();
        if (!owner || !owner->registered || owner->ended) { reject("Unknown or expired terminal lease"); return; }
        const auto error = hook(*owner, message.value("event").toObject());
        reply(*client, {{"ok", error.isEmpty()}, {"error", error}}, true);
        if (!error.isEmpty()) emit diagnostic("Interactive hook rejected: " + error);
        return;
    }
    if (op != "attach") { reject("Expected attach or hook"); return; }
    const auto directory = message.value("project").toString();
    const QFileInfo path(directory);
    const auto id = message.value("sessionId").toString();
    if (!path.isAbsolute() || !path.isDir() || !path.isReadable() || directory.size() > 4096
        || (!id.isEmpty() && !identifier(id))) { reject("Invalid project or session ID"); return; }
    // Reject duplicate active leases before touching the registry or credentials.
    for (const auto& existing : clients_) {
        if (existing != client && !id.isEmpty() && existing->id == id && existing->bridge) {
            reject("Session already attached to another terminal"); return;
        }
    }
    try {
        const auto registration = registration_(path.canonicalFilePath(), id);
        if (!registration.bridge) { reject("Session unavailable or capacity reached"); return; }
        client->id = registration.id;
        client->directory = path.canonicalFilePath();
        client->bridge = registration.bridge;
        client->bindIdentity = registration.bindIdentity;
        client->token = QUuid::createUuid().toString(QUuid::Id128) + QUuid::createUuid().toString(QUuid::Id128);
        connect(registration.bridge, &ipc::BridgeClient::registered, this, [this, client] {
            if (!client->socket || client->socket->state() != QLocalSocket::ConnectedState) { release(client); return; }
            client->registered = true; client->deadline.start(10000);
            tokens_.insert(client->token, client);
            reply(*client, {{"ok", true}, {"sessionId", client->id}, {"token", client->token}});
            emit diagnostic("Interactive terminal registered; waiting for trusted Codex SessionStart hook");
        });
        const auto fail = [this, client] {
            if (client->socket) { reply(*client, {{"ok", false}, {"error", "Pet connection lost"}}, true); }
        };
        connect(registration.bridge, &ipc::BridgeClient::failed, this, fail);
        connect(registration.bridge, &ipc::BridgeClient::disconnected, this, fail);
        connect(registration.bridge, &ipc::BridgeClient::messageReceived, this, [fail](const ipc::Message& incoming) {
            if (incoming.type == "error") fail();
        });
        registration.bridge->connectToServer();
    } catch (const std::exception&) { reject("Cannot register interactive session"); }
}
QString InteractiveServer::hook(Client& owner, const QJsonObject& event)
{
    const auto name = event.value("hook_event_name").toString();
    const auto codexId = event.value("session_id").toString();
    if (!identifier(codexId) || QFileInfo(event.value("cwd").toString()).canonicalFilePath() != owner.directory)
        return "Invalid Codex identity or project";
    if (!owner.bridge || !owner.bridge->isReady()) return "Pet is disconnected";
    if (name == "SessionStart") {
        for (const auto& other : clients_)
            if (other.get() != &owner && !other->ended && other->codexId == codexId && other->bridge)
                return "Codex session already bound to another terminal";
        if (!owner.codexId.isEmpty()) {
            if (owner.codexId == codexId) return {}; // Includes in-turn compaction.
            owner.ended = true;
            if (owner.socket) owner.socket->disconnectFromServer();
            return "Codex identity changed; relaunch the wrapper for the new session";
        }
        try {
            if (!owner.bindIdentity || !owner.bindIdentity(codexId)) return "Codex identity does not match the reserved pet";
        } catch (const std::exception&) { return "Cannot persist Codex identity"; }
        if (!owner.bridge->sendEvent("SessionConnected")) return "Delivery failed";
        owner.codexId = codexId; return {};
    }
    if (owner.codexId != codexId) return "SessionStart required for this Codex identity";
    if (name == "SessionEnd") {
        owner.ended = true;
        if (owner.socket) owner.socket->disconnectFromServer();
        return {};
    }
    const auto turn = event.value("turn_id").toString();
    if (!identifier(turn)) return "Missing turn ID";
    if (name == "UserPromptSubmit") {
        if (owner.finishedTurns.contains(turn)) return "Stale turn";
        if (owner.working) return owner.turn == turn ? QString{} : QString("Previous turn still open");
        if (owner.finishedTurns.size() >= 4096) return "Turn limit reached; relaunch wrapper";
        if (!owner.bridge->sendEvent("TaskStarted")) return "Delivery failed";
        owner.turn = turn; owner.working = true; return {};
    }
    if (name != "Stop" && name != "Interrupt") return "Unsupported hook";
    if (owner.finishedTurns.contains(turn)) return {}; // Idempotent terminal hook retry.
    if (!owner.working || owner.turn != turn) return "Out-of-order turn boundary";
    if (!owner.bridge->sendEvent(name == "Stop" ? "TaskCompleted" : "TaskInterrupted")) return "Delivery failed";
    owner.finishedTurns.insert(turn); owner.working = false; return {};
}
}
