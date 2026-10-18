#include "dimepenny/ipc/protocol.h"
#include "dimepenny/domain/session_id.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStringConverter>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

namespace dimepenny::ipc {
namespace {
// Qt's JSON parser keeps the last duplicate member. Reject ambiguous wire input
// before it loses that information, including escaped spellings of the same key.
void uniqueKeys(const QByteArray& bytes)
{
    std::vector<QSet<QString>> scopes;
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        const char ch = bytes[i];
        if (ch == '{') scopes.emplace_back();
        else if (ch == '}') { if (!scopes.empty()) scopes.pop_back(); }
        else if (ch == '"') {
            const auto start = i;
            while (++i < bytes.size()) {
                if (bytes[i] == '\\') ++i;
                else if (bytes[i] == '"') break;
            }
            auto next = i + 1;
            while (next < bytes.size() && (bytes[next] == ' ' || bytes[next] == '\n'
                   || bytes[next] == '\r' || bytes[next] == '\t')) ++next;
            if (next < bytes.size() && bytes[next] == ':' && !scopes.empty()) {
                const auto token = bytes.mid(start, i - start + 1);
                const auto key = QJsonDocument::fromJson("[" + token + "]").array().first().toString();
                if (scopes.back().contains(key)) throw ProtocolError("Duplicate JSON member");
                scopes.back().insert(key);
            }
        }
    }
}

void require(bool condition, const char* reason)
{
    if (!condition) throw ProtocolError(reason);
}

void keys(const QJsonObject& object, std::initializer_list<const char*> required,
          std::initializer_list<const char*> optional = {})
{
    for (const auto* key : required) require(object.contains(key), "Missing required field");
    for (auto it = object.begin(); it != object.end(); ++it) {
        const auto matches = [&](const char* key) { return it.key() == QLatin1String(key); };
        require(std::any_of(required.begin(), required.end(), matches)
                || std::any_of(optional.begin(), optional.end(), matches), "Unknown field");
    }
}

QString text(const QJsonObject& object, const char* key, qsizetype limit = 256)
{
    const auto value = object.value(key);
    require(value.isString(), "Expected string");
    const auto result = value.toString();
    require(!result.trimmed().isEmpty() && result.toUtf8().size() <= limit, "Invalid string length");
    for (const auto c : result) require(c.unicode() >= 32 && c.unicode() != 127, "Control character in text");
    require(result.isValidUtf16(), "Invalid Unicode string");
    return result;
}

void identifier(const QString& value)
{
    try { (void)SessionId{value.toStdString()}; }
    catch (const std::invalid_argument&) { throw ProtocolError("Invalid identifier"); }
}

void timestamp(const QString& value)
{
    static const QRegularExpression pattern{R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$)"};
    const auto parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    require(pattern.match(value).hasMatch() && parsed.isValid()
            && parsed.toUTC().toString(Qt::ISODateWithMs) == value, "Invalid UTC timestamp");
}

void integer(const QJsonValue& value, double minimum, double maximum)
{
    require(value.isDouble(), "Expected integer");
    const auto number = value.toDouble();
    require(std::isfinite(number) && number >= minimum && number <= maximum
            && std::floor(number) == number, "Integer out of range");
}

QJsonObject object(const QJsonObject& parent, const char* key)
{
    require(parent.value(key).isObject(), "Expected object");
    return parent.value(key).toObject();
}

void decision(const QJsonObject& payload)
{
    identifier(text(payload, "requestId", 128));
    const auto value = text(payload, "decision");
    require(value == "approve" || value == "deny", "Unknown approval decision");
}
} // namespace

void validate(const Message& message)
{
    require(message.version == protocolVersion, "Unsupported protocol version");
    identifier(message.messageId);
    identifier(message.sessionId);
    timestamp(message.timestamp);
    const auto& p = message.payload;
    if (message.type == "register") {
        keys(p, {"project", "pet", "terminalType"}, {"codexSessionId", "processId", "authToken"});
        if (p.contains("authToken")) {
            static const QRegularExpression token{R"(^[0-9a-f]{64}$)"};
            require(token.match(text(p, "authToken", 64)).hasMatch(), "Invalid authentication token");
        }
        (void)text(p, "terminalType");
        if (p.contains("codexSessionId")) (void)text(p, "codexSessionId");
        if (p.contains("processId")) integer(p.value("processId"), 1, 4294967295.0);
        const auto project = object(p, "project");
        keys(project, {"name", "directory"});
        (void)text(project, "name");
        const auto path = text(project, "directory", 4096);
        // Windows drive-rooted and UNC paths; existence/authorization is not a codec concern.
        static const QRegularExpression drive{R"(^[A-Za-z]:[\\/])"};
        const bool unc = path.startsWith("\\\\") && path.mid(2).split('\\', Qt::SkipEmptyParts).size() >= 2;
        require(drive.match(path).hasMatch() || unc, "Expected absolute Windows project path");
        const auto pet = object(p, "pet");
        keys(pet, {"id", "name", "appearanceKey", "color"});
        (void)text(pet, "id");
        (void)text(pet, "name");
        (void)text(pet, "appearanceKey");
        const auto color = object(pet, "color");
        keys(color, {"red", "green", "blue"});
        for (const auto* channel : {"red", "green", "blue"}) integer(color.value(channel), 0, 255);
    } else if (message.type == "registration_result") {
        keys(p, {"inReplyTo", "accepted"}, {"reason"});
        identifier(text(p, "inReplyTo", 128));
        require(p.value("accepted").isBool(), "Expected accepted boolean");
        if (!p.value("accepted").toBool()) (void)text(p, "reason", 2048);
        else require(!p.contains("reason"), "Accepted registration cannot have rejection reason");
    } else if (message.type == "event") {
        keys(p, {"eventType", "sequence"}, {"approval"});
        // Decimal string avoids JSON double precision loss for uint64 counters.
        const auto sequence = text(p, "sequence", 20);
        static const QRegularExpression digits{R"(^(0|[1-9][0-9]*)$)"};
        bool ok = false;
        (void)sequence.toULongLong(&ok);
        require(ok && digits.match(sequence).hasMatch(), "Invalid sequence");
        const auto type = text(p, "eventType");
        const bool approval = type == "ApprovalRequested" || type == "ApprovalResolved";
        require(approval || type == "SessionConnected" || type == "TaskStarted"
                || type == "TaskCompleted" || type == "SessionDisconnected" || type == "SessionError",
                "Unknown lifecycle event");
        if (approval) {
            const auto details = object(p, "approval");
            if (type == "ApprovalResolved") {
                keys(details, {"requestId", "decision"});
                decision(details);
            } else {
                keys(details, {"requestId"});
                identifier(text(details, "requestId", 128));
            }
        } else require(!p.contains("approval"), "Unexpected approval metadata");
    } else if (message.type == "heartbeat") {
        keys(p, {"kind"}, {"inReplyTo"});
        const auto kind = text(p, "kind");
        require(kind == "ping" || kind == "pong", "Unknown heartbeat kind");
        if (kind == "pong") identifier(text(p, "inReplyTo", 128));
        else require(!p.contains("inReplyTo"), "Ping cannot reply to another message");
    } else if (message.type == "disconnect") {
        keys(p, {"reason"});
        (void)text(p, "reason", 2048);
    } else if (message.type == "error") {
        keys(p, {"code", "message"}, {"inReplyTo"});
        identifier(text(p, "code", 128));
        (void)text(p, "message", 2048);
        if (p.contains("inReplyTo")) identifier(text(p, "inReplyTo", 128));
    } else if (message.type == "approval_response") {
        keys(p, {"requestId", "decision"});
        decision(p);
    } else throw ProtocolError("Unknown message type");
}

QByteArray encodeJson(const Message& message)
{
    validate(message);
    const auto bytes = QJsonDocument{QJsonObject{
        {"protocolVersion", message.version}, {"messageId", message.messageId},
        {"sessionId", message.sessionId}, {"timestamp", message.timestamp},
        {"type", message.type}, {"payload", message.payload}}}.toJson(QJsonDocument::Compact);
    require(bytes.size() <= maxMessageBytes, "Message too large");
    return bytes;
}

Message decodeJson(const QByteArray& bytes)
{
    require(!bytes.isEmpty() && bytes.size() <= maxMessageBytes, "Invalid message size");
    QStringDecoder utf8{QStringDecoder::Utf8};
    const QString decoded = utf8(bytes);
    require(!utf8.hasError() && decoded.toUtf8() == bytes, "Invalid UTF-8");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && document.isObject(), "Invalid JSON object");
    uniqueKeys(bytes);
    const auto root = document.object();
    keys(root, {"protocolVersion", "messageId", "sessionId", "timestamp", "type", "payload"});
    integer(root.value("protocolVersion"), protocolVersion, protocolVersion);
    Message message{protocolVersion, text(root, "messageId", 128), text(root, "sessionId", 128),
                    text(root, "timestamp", 24), text(root, "type", 32), object(root, "payload")};
    validate(message);
    return message;
}

QByteArray encodeFrame(const Message& message)
{
    const auto body = encodeJson(message);
    QByteArray frame(4, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(body.size()), frame.data());
    frame.append(body);
    return frame;
}

std::vector<Message> FrameDecoder::feed(const QByteArray& bytes)
{
    require(!failed_, "Decoder is failed");
    try {
        require(bytes.size() <= maxReadBytes, "Read chunk too large");
        std::vector<Message> messages;
        qsizetype offset = 0;
        while (offset < bytes.size()) {
            const auto target = expected_ == 0 ? qsizetype{4} : expected_;
            const auto count = std::min(target - pending_.size(), bytes.size() - offset);
            pending_.append(bytes.constData() + offset, count);
            offset += count;
            if (pending_.size() != target) continue;
            if (expected_ == 0) {
                expected_ = qFromBigEndian<quint32>(pending_.constData());
                require(expected_ > 0 && expected_ <= maxMessageBytes, "Invalid frame length");
                pending_.clear();
            } else {
                messages.push_back(decodeJson(pending_));
                pending_.clear();
                expected_ = 0;
            }
        }
        return messages;
    } catch (...) {
        failed_ = true;
        pending_.clear();
        expected_ = 0;
        throw;
    }
}

void FrameDecoder::finish()
{
    require(!failed_, "Decoder is failed");
    if (!pending_.isEmpty() || expected_ != 0) {
        failed_ = true;
        pending_.clear();
        expected_ = 0;
        throw ProtocolError("Truncated frame at EOF");
    }
}
} // namespace dimepenny::ipc
