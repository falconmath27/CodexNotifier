#pragma once
#include "dimepenny/ipc/simulated_bridge.h"
#include <QLocalServer>
#include <QPointer>
#include <QSet>
#include <functional>
#include <memory>

namespace dimepenny {
struct InteractiveRegistration {
    QString id;
    ipc::BridgeClient* bridge = nullptr;
    std::function<bool(const QString&)> bindIdentity;
};

// Opt-in, current-user-only bootstrap and passive hook receiver. The long-lived
// owner socket is a lease: losing it disconnects only its assigned session.
class InteractiveServer final : public QObject {
    Q_OBJECT
public:
    using Register = std::function<InteractiveRegistration(const QString&, const QString&)>;
    explicit InteractiveServer(Register registration, QObject* parent = nullptr);
    ~InteractiveServer() override;
    static QString defaultEndpoint();
    bool listen(const QString& endpoint);
    void stop();
    QString errorString() const { return server_.errorString(); }
signals:
    void diagnostic(const QString& message);
private:
    struct Client {
        QByteArray buffer;
        QPointer<QLocalSocket> socket;
        QPointer<ipc::BridgeClient> bridge;
        QTimer deadline;
        QString token, id, directory, codexId, turn;
        QSet<QString> finishedTurns;
        std::function<bool(const QString&)> bindIdentity;
        bool requested = false, registered = false, working = false, ended = false;
    };
    Register registration_;
    QLocalServer server_;
    QHash<QLocalSocket*, std::shared_ptr<Client>> clients_;
    QHash<QString, std::weak_ptr<Client>> tokens_;
    void accept();
    void read(const std::shared_ptr<Client>& client);
    void request(const std::shared_ptr<Client>& client, const QJsonObject& message);
    QString hook(Client& owner, const QJsonObject& event);
    void reply(Client& client, QJsonObject message, bool close = false);
    void release(const std::shared_ptr<Client>& client);
};
}
