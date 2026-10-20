#pragma once
#include "dimepenny/ipc/local_server.h"
#include <optional>

namespace dimepenny::ipc {
// Simulator only: never reads or controls a real Codex process.
class SimulatedBridge final : public QObject {
    Q_OBJECT
public:
    SimulatedBridge(QString endpoint, QString sessionId, QString token,
                    QJsonObject registrationPayload, QObject* parent = nullptr);
    void connectToServer(); // Reconnect retains sequence and last event.
    void resumeAfter(std::uint64_t lastSequence); // Before the first connection only.
    void disconnectFromServer();
    [[nodiscard]] bool isReady() const noexcept { return ready_; }
    [[nodiscard]] bool sendEvent(const QString& type, QJsonObject approval = {});
    [[nodiscard]] bool replayLastEvent();
    [[nodiscard]] bool sendMalformedFrame();
signals:
    void registered();
    void disconnected();
    void failed(const QString& reason);
    void messageReceived(const dimepenny::ipc::Message& message);
private:
    QString endpoint_, sessionId_, registrationId_;
    QJsonObject payload_;
    QLocalSocket socket_;
    QTimer deadline_;
    FrameDecoder decoder_;
    quint64 sequence_ = 0;
    bool exhausted_ = false;
    bool ready_ = false;
    bool closing_ = false;
    std::optional<Message> lastEvent_;
    Message make(const QString& type, QJsonObject payload) const;
    bool write(const Message& message);
    void read();
    void fail(const QString& reason);
};
}
