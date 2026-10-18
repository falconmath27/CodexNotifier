#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <stdexcept>
#include <vector>

namespace dimepenny::ipc {

inline constexpr int protocolVersion = 1;
inline constexpr qsizetype maxMessageBytes = 64 * 1024;
inline constexpr qsizetype maxReadBytes = maxMessageBytes + 4;

class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Payloads are validated JSON objects. Domain dispatch is a later application layer.
struct Message {
    int version = protocolVersion;
    QString messageId;
    QString sessionId;
    QString timestamp;
    QString type;
    QJsonObject payload;
};

void validate(const Message& message);
[[nodiscard]] QByteArray encodeJson(const Message& message);
[[nodiscard]] Message decodeJson(const QByteArray& json);
[[nodiscard]] QByteArray encodeFrame(const Message& message);

// One instance per connection. Failed decoders must be replaced on a new connection.
class FrameDecoder final {
public:
    [[nodiscard]] std::vector<Message> feed(const QByteArray& bytes);
    void finish(); // Call at EOF; rejects an incomplete header/body.
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] qsizetype bufferedBytes() const noexcept { return pending_.size(); }
    [[nodiscard]] bool hasIncompleteFrame() const noexcept { return expected_ != 0 || !pending_.isEmpty(); }

private:
    QByteArray pending_;
    qsizetype expected_ = 0;
    bool failed_ = false;
};

} // namespace dimepenny::ipc
