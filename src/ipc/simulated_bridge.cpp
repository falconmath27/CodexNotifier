#include "dimepenny/ipc/simulated_bridge.h"
#include <QDateTime>
#include <QUuid>
#include <limits>

namespace dimepenny::ipc {
SimulatedBridge::SimulatedBridge(QString endpoint, QString sessionId, QString token,
                               QJsonObject registrationPayload, QObject* parent)
    : QObject(parent), endpoint_(std::move(endpoint)), sessionId_(std::move(sessionId)),
      payload_(std::move(registrationPayload))
{
    if (endpoint_.isEmpty()) throw std::invalid_argument("Endpoint is required");
    payload_.insert("authToken", token);
    validate(make("register", payload_));
    socket_.setReadBufferSize(maxReadBytes);
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] { fail("server_timeout"); });
    connect(&socket_, &QLocalSocket::connected, this, [this] {
        const auto registration = make("register", payload_);
        registrationId_ = registration.messageId;
        (void)write(registration);
    });
    connect(&socket_, &QLocalSocket::readyRead, this, &SimulatedBridge::read);
    connect(&socket_, &QLocalSocket::disconnected, this, [this] {
        if (!ready_ && !closing_) emit failed("connection_closed_before_registration");
        ready_ = false; deadline_.stop(); emit disconnected();
    });
    connect(&socket_, &QLocalSocket::errorOccurred, this, [this](auto) { fail("socket_error"); });
}
Message SimulatedBridge::make(const QString& type, QJsonObject payload) const
{
    return {protocolVersion, QUuid::createUuid().toString(QUuid::WithoutBraces), sessionId_,
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), type, std::move(payload)};
}
void SimulatedBridge::connectToServer()
{
    if (socket_.state() != QLocalSocket::UnconnectedState) return;
    ready_ = false; closing_ = false; decoder_ = FrameDecoder{}; registrationId_.clear();
    deadline_.start(5000);
    socket_.connectToServer(endpoint_);
}
void SimulatedBridge::fail(const QString& reason)
{
    ready_ = false; closing_ = true; deadline_.stop(); emit failed(reason); socket_.abort();
}
bool SimulatedBridge::write(const Message& message)
{
    if (socket_.state() != QLocalSocket::ConnectedState) return false;
    const auto frame = encodeFrame(message);
    if (socket_.bytesToWrite() + frame.size() > 4 * maxReadBytes) { fail("output_limit"); return false; }
    if (socket_.write(frame) != frame.size()) { fail("write_error"); return false; }
    return true;
}
void SimulatedBridge::read()
{
    if (socket_.state() != QLocalSocket::ConnectedState) return;
    try {
        const auto messages = decoder_.feed(socket_.read(maxReadBytes));
        for (const auto& message : messages) {
            if (message.sessionId != sessionId_) { fail("session_mismatch"); return; }
            if (!ready_) {
                if (message.type != "registration_result"
                    || message.payload.value("inReplyTo") != registrationId_
                    || !message.payload.value("accepted").toBool()) { fail("registration_rejected"); return; }
                ready_ = true; deadline_.start(20000); emit registered();
            } else if (message.type == "heartbeat" && message.payload.value("kind") == "ping") {
                if (!write(make("heartbeat", {{"kind", "pong"}, {"inReplyTo", message.messageId}}))) return;
            } else if (message.type == "error" || message.type == "approval_response") {
                emit messageReceived(message);
            } else { fail("unexpected_server_message"); return; }
            if (socket_.state() != QLocalSocket::ConnectedState) return;
            deadline_.start(20000);
        }
        if (socket_.bytesAvailable()) QTimer::singleShot(0, this, &SimulatedBridge::read);
    } catch (const ProtocolError&) { fail("invalid_protocol"); }
}
bool SimulatedBridge::sendEvent(const QString& type, QJsonObject approval)
{
    if (!ready_ || exhausted_) return false;
    QJsonObject payload{{"eventType", type}, {"sequence", QString::number(sequence_)}};
    if (!approval.isEmpty()) payload.insert("approval", approval);
    const auto event = make("event", payload);
    if (!write(event)) return false;
    lastEvent_ = event;
    if (sequence_ == std::numeric_limits<quint64>::max()) exhausted_ = true;
    else ++sequence_;
    return true;
}
bool SimulatedBridge::replayLastEvent() { return ready_ && lastEvent_ && write(*lastEvent_); }
bool SimulatedBridge::sendMalformedFrame()
{
    if (!ready_) return false;
    const auto bytes = QByteArray::fromHex("000000017b"); // Unterminated JSON object.
    if (socket_.bytesToWrite() + bytes.size() > 4 * maxReadBytes) return false;
    return socket_.write(bytes) == bytes.size();
}
void SimulatedBridge::disconnectFromServer()
{
    closing_ = true;
    if (ready_) (void)write(make("disconnect", {{"reason", "simulated_session_finished"}}));
    ready_ = false; socket_.disconnectFromServer();
}
}
