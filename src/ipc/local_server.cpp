#include "dimepenny/ipc/local_server.h"
#include "dimepenny/domain/session_id.h"
#include <QDateTime>
#include <QRandomGenerator>
#include <QUuid>

namespace dimepenny::ipc {
struct LocalServer::Client {
    QLocalSocket* socket;
    FrameDecoder decoder;
    QString session;
    QString pingId;
    qint64 connectedAt;
    qint64 lastPing;
    qint64 partialAt = -1;
    bool closed = false;
};

namespace {
Message outgoing(const QString& session, const QString& type, QJsonObject payload)
{
    return {protocolVersion, QUuid::createUuid().toString(QUuid::WithoutBraces), session,
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), type, std::move(payload)};
}
bool equalSecret(const QString& a, const QString& b)
{
    if (a.size() != 64 || b.size() != 64) return false;
    unsigned difference = 0;
    for (qsizetype i = 0; i < 64; ++i) difference |= a[i].unicode() ^ b[i].unicode();
    return difference == 0;
}
}

LocalServer::LocalServer(ServerOptions options, QObject* parent)
    : QObject(parent), options_(options)
{
    if (options.handshakeTimeoutMs <= 0 || options.heartbeatIntervalMs <= 0
        || options.heartbeatTimeoutMs <= 0 || options.frameTimeoutMs <= 0 || options.tickMs <= 0)
        throw std::invalid_argument("Server timeouts must be positive");
    qRegisterMetaType<Message>();
    listener_.setSocketOptions(QLocalServer::UserAccessOption);
    listener_.setMaxPendingConnections(10);
    connect(&listener_, &QLocalServer::newConnection, this, &LocalServer::acceptClients);
    connect(&timer_, &QTimer::timeout, this, &LocalServer::tick);
    clock_.start();
}
LocalServer::~LocalServer() { stop(); }

bool LocalServer::listen(const QString& endpoint)
{
    if (listener_.isListening() || endpoint.isEmpty()) return false;
    // Never remove another application's endpoint to make a listen succeed.
    if (!listener_.listen(endpoint)) return false;
    timer_.start(options_.tickMs);
    return true;
}
void LocalServer::stop()
{
    timer_.stop();
    listener_.close();
    const auto clients = clients_.values();
    for (const auto& client : clients) drop(client, "server_stopped");
    credentials_.clear();
}
QString LocalServer::authorize(const QString& sessionId)
{
    (void)SessionId{sessionId.toStdString()};
    QByteArray secret;
    for (int i = 0; i < 8; ++i) {
        const auto word = QRandomGenerator::system()->generate();
        secret.append(reinterpret_cast<const char*>(&word), sizeof(word));
    }
    const auto token = QString::fromLatin1(secret.toHex());
    revoke(sessionId);
    credentials_.insert(sessionId, token);
    return token;
}
void LocalServer::revoke(const QString& sessionId)
{
    credentials_.remove(sessionId);
    const auto clients = clients_.values();
    for (const auto& client : clients) if (client->session == sessionId) drop(client, "credential_revoked");
}
void LocalServer::acceptClients()
{
    while (auto* socket = listener_.nextPendingConnection()) {
        if (clients_.size() >= 10) { socket->abort(); socket->deleteLater(); continue; }
        socket->setReadBufferSize(maxReadBytes);
        const auto now = clock_.elapsed();
        auto client = std::make_shared<Client>();
        client->socket = socket;
        client->connectedAt = now;
        client->lastPing = now;
        clients_.insert(socket, client);
        connect(socket, &QLocalSocket::readyRead, this, [this, client] { read(client); });
        connect(socket, &QLocalSocket::disconnected, this, [this, client] {
            if (client->closed) return;
            try { client->decoder.finish(); drop(client, "connection_closed"); }
            catch (const ProtocolError&) { drop(client, "truncated_frame"); }
        });
        connect(socket, &QLocalSocket::errorOccurred, this, [this, client](auto) {
            drop(client, "socket_error");
        });
        if (socket->bytesAvailable()) read(client);
    }
}
void LocalServer::drop(const std::shared_ptr<Client>& client, const QString& reason)
{
    if (client->closed) return;
    client->closed = true;
    clients_.remove(client->socket);
    client->socket->disconnect(this);
    client->socket->abort();
    client->socket->deleteLater();
    if (!client->session.isEmpty()) emit sessionDisconnected(client->session, reason);
}
bool LocalServer::write(const std::shared_ptr<Client>& client, const Message& message)
{
    if (client->closed) return false;
    const auto bytes = encodeFrame(message);
    if (client->socket->bytesToWrite() + bytes.size() > 4 * maxReadBytes) {
        drop(client, "output_limit"); return false;
    }
    if (client->socket->write(bytes) != bytes.size()) { drop(client, "write_error"); return false; }
    return true;
}
bool LocalServer::send(const Message& message)
{
    if (message.type != "approval_response" && message.type != "error") return false;
    const auto clients = clients_.values();
    for (const auto& client : clients)
        if (client->session == message.sessionId) return write(client, message);
    return false;
}
void LocalServer::read(const std::shared_ptr<Client>& client)
{
    if (client->closed) return;
    const auto now = clock_.elapsed();
    if (client->session.isEmpty() && now - client->connectedAt >= options_.handshakeTimeoutMs) {
        drop(client, "handshake_timeout"); return;
    }
    if (!client->pingId.isEmpty() && now - client->lastPing >= options_.heartbeatTimeoutMs) {
        drop(client, "heartbeat_timeout"); return;
    }
    if (client->partialAt >= 0 && now - client->partialAt >= options_.frameTimeoutMs) {
        drop(client, "frame_timeout"); return;
    }
    try {
        const auto messages = client->decoder.feed(client->socket->read(maxReadBytes));
        for (const auto& message : messages) {
            handle(client, message);
            if (client->closed) return;
        }
        if (!client->decoder.hasIncompleteFrame()) client->partialAt = -1;
        else if (client->partialAt < 0 || !messages.empty()) client->partialAt = clock_.elapsed();
        if (client->socket->bytesAvailable())
            QTimer::singleShot(0, this, [this, client] { read(client); });
    } catch (const ProtocolError&) { drop(client, "invalid_protocol"); }
}
void LocalServer::handle(const std::shared_ptr<Client>& client, Message message)
{
    if (client->session.isEmpty()) {
        if (message.type != "register" || !credentials_.contains(message.sessionId)
            || !equalSecret(credentials_.value(message.sessionId), message.payload.value("authToken").toString())) {
            drop(client, "authentication_failed"); return;
        }
        for (const auto& other : clients_)
            if (other != client && other->session == message.sessionId) {
                drop(client, "session_already_connected"); return;
            }
        client->session = message.sessionId;
        message.payload.remove("authToken"); // Never expose credentials to application observers.
        if (!write(client, outgoing(client->session, "registration_result",
                    {{"inReplyTo", message.messageId}, {"accepted", true}}))) return;
        emit sessionAuthenticated(client->session);
        if (!client->closed) emit messageReceived(message);
        return;
    }
    if (message.sessionId != client->session) { drop(client, "session_mismatch"); return; }
    if (message.type == "heartbeat") {
        if (message.payload.value("kind") == "ping") {
            (void)write(client, outgoing(client->session, "heartbeat",
                        {{"kind", "pong"}, {"inReplyTo", message.messageId}}));
        } else if (client->pingId.isEmpty() || message.payload.value("inReplyTo") != client->pingId) {
            drop(client, "heartbeat_mismatch");
        } else { client->pingId.clear(); client->lastPing = clock_.elapsed(); }
    } else if (message.type == "disconnect") {
        drop(client, "client_disconnect");
    } else if (message.type == "event" || message.type == "error") {
        emit messageReceived(message);
    } else drop(client, "invalid_direction");
}
void LocalServer::tick()
{
    const auto now = clock_.elapsed();
    const auto clients = clients_.values();
    for (const auto& client : clients) {
        if (client->session.isEmpty()) {
            if (now - client->connectedAt >= options_.handshakeTimeoutMs) drop(client, "handshake_timeout");
        } else if (!client->pingId.isEmpty()) {
            if (now - client->lastPing >= options_.heartbeatTimeoutMs) drop(client, "heartbeat_timeout");
        } else if (now - client->lastPing >= options_.heartbeatIntervalMs) {
            const auto ping = outgoing(client->session, "heartbeat", {{"kind", "ping"}});
            client->pingId = ping.messageId;
            client->lastPing = now;
            (void)write(client, ping);
        }
        if (!client->closed && client->partialAt >= 0 && now - client->partialAt >= options_.frameTimeoutMs)
            drop(client, "frame_timeout");
    }
}
} // namespace dimepenny::ipc
