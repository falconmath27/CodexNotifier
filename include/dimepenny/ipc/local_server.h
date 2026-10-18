#pragma once

#include "dimepenny/ipc/protocol.h"
#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QElapsedTimer>
#include <QTimer>
#include <QHash>
#include <memory>

namespace dimepenny::ipc {

struct ServerOptions {
    int handshakeTimeoutMs = 5000;
    int heartbeatIntervalMs = 5000;
    int heartbeatTimeoutMs = 15000;
    int frameTimeoutMs = 5000;
    int tickMs = 100;
};

// Use from one Qt event-loop thread. No credentials are created by remote clients.
class LocalServer final : public QObject {
    Q_OBJECT
public:
    explicit LocalServer(ServerOptions options = {}, QObject* parent = nullptr);
    ~LocalServer() override;
    [[nodiscard]] bool listen(const QString& endpoint);
    void stop();
    [[nodiscard]] QString errorString() const { return listener_.errorString(); }
    // Trusted caller provisions/rotates a 256-bit secret for exactly one session.
    // Rotation disconnects any existing connection for that session.
    [[nodiscard]] QString authorize(const QString& sessionId);
    void revoke(const QString& sessionId);
    [[nodiscard]] int connectionCount() const { return static_cast<int>(clients_.size()); }
    [[nodiscard]] bool send(const Message& message);

signals:
    void sessionAuthenticated(const QString& sessionId);
    void messageReceived(const dimepenny::ipc::Message& message);
    void sessionDisconnected(const QString& sessionId, const QString& reason);

private:
    struct Client;
    ServerOptions options_;
    QLocalServer listener_;
    QTimer timer_;
    QElapsedTimer clock_;
    QHash<QString, QString> credentials_;
    QHash<QLocalSocket*, std::shared_ptr<Client>> clients_;
    void acceptClients();
    void read(const std::shared_ptr<Client>& client);
    void handle(const std::shared_ptr<Client>& client, Message message);
    void drop(const std::shared_ptr<Client>& client, const QString& reason);
    bool write(const std::shared_ptr<Client>& client, const Message& message);
    void tick();
};
} // namespace dimepenny::ipc

Q_DECLARE_METATYPE(dimepenny::ipc::Message)
