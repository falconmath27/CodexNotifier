#include "dimepenny/ipc/protocol.h"
#include <QJsonDocument>
#include <QtEndian>
#include <iostream>

namespace {
using namespace dimepenny::ipc;
int checks = 0;
void check(bool value)
{
    ++checks;
    if (!value) throw std::runtime_error("Failed check " + std::to_string(checks));
}
template <typename F> void rejects(F action)
{
    try { action(); }
    catch (const ProtocolError&) { check(true); return; }
    check(false);
}
Message message(QString type = "heartbeat", QJsonObject payload = {{"kind", "ping"}})
{
    return {1, "m-1", "s-1", "2026-10-18T10:00:00.000Z", std::move(type), std::move(payload)};
}
QByteArray rawFrame(const QByteArray& body)
{
    QByteArray frame(4, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(body.size()), frame.data());
    return frame + body;
}
void roundTrips()
{
    const QJsonObject registration{
        {"project", QJsonObject{{"name", "Dime & Penny"}, {"directory", "C:\\Projects\\Dime"}}},
        {"pet", QJsonObject{{"id", "pet-1"}, {"name", QString::fromUtf8("Penny \xE2\x98\x85")},
            {"appearanceKey", "round"}, {"color", QJsonObject{{"red", 0}, {"green", 100}, {"blue", 255}}}}},
        {"terminalType", "Windows Terminal"}, {"processId", 123}, {"codexSessionId", "codex-1"}};
    std::vector<Message> examples{
        message("register", registration),
        message("registration_result", {{"inReplyTo", "m-0"}, {"accepted", true}}),
        message("registration_result", {{"inReplyTo", "m-0"}, {"accepted", false}, {"reason", "Capacity reached"}}),
        message(), message("heartbeat", {{"kind", "pong"}, {"inReplyTo", "m-0"}}),
        message("disconnect", {{"reason", "User exited"}}),
        message("error", {{"code", "invalid_state"}, {"message", "Event rejected"}, {"inReplyTo", "m-0"}}),
        message("approval_response", {{"requestId", "r-1"}, {"decision", "deny"}})};
    for (const auto* type : {"SessionConnected", "TaskStarted", "TaskCompleted", "ApprovalRequested",
                             "ApprovalResolved", "SessionDisconnected", "SessionError", "TaskInterrupted"}) {
        QJsonObject payload{{"eventType", type}, {"sequence", "18446744073709551615"}};
        if (QString{type} == "ApprovalRequested") payload.insert("approval", QJsonObject{{"requestId", "r-1"}});
        if (QString{type} == "ApprovalResolved") payload.insert("approval", QJsonObject{{"requestId", "r-1"}, {"decision", "approve"}});
        examples.push_back(message("event", payload));
    }
    for (const auto& example : examples) {
        const auto json = encodeJson(example);
        const auto decoded = decodeJson(json);
        check(encodeJson(decoded) == json);
        check(decoded.payload == example.payload);
        const auto frame = encodeFrame(example);
        check(qFromBigEndian<quint32>(frame.constData()) == static_cast<quint32>(json.size()));
        FrameDecoder stream;
        std::vector<Message> received;
        for (const auto byte : frame) {
            auto batch = stream.feed(QByteArray(1, byte));
            received.insert(received.end(), batch.begin(), batch.end());
            check(stream.bufferedBytes() <= maxMessageBytes);
        }
        stream.finish();
        check(received.size() == 1 && encodeJson(received.front()) == json);
    }
    FrameDecoder multi;
    const auto frame = encodeFrame(message());
    check(multi.feed(frame + frame + frame).size() == 3);
    multi.finish();
    // Each possible two-part split, including within the length prefix.
    for (qsizetype split = 0; split <= frame.size(); ++split) {
        FrameDecoder stream;
        const auto first = stream.feed(frame.left(split));
        const auto second = stream.feed(frame.mid(split));
        check(first.size() + second.size() == 1);
        stream.finish();
    }
}
void invalidMessages()
{
    const auto json = encodeJson(message());
    auto root = QJsonDocument::fromJson(json).object();
    for (const auto& key : root.keys()) {
        auto missing = root; missing.remove(key);
        rejects([&] { (void)decodeJson(QJsonDocument{missing}.toJson()); });
        auto wrong = root; wrong.insert(key, QJsonValue::Null);
        rejects([&] { (void)decodeJson(QJsonDocument{wrong}.toJson()); });
    }
    for (const auto version : {0.0, 2.0, 1.5}) {
        auto changed = root; changed.insert("protocolVersion", version);
        rejects([&] { (void)decodeJson(QJsonDocument{changed}.toJson()); });
    }
    for (const auto* value : {"", "2026-02-30T10:00:00.000Z", "2026-10-18T10:00:00+05:30", "2026-10-18"}) {
        auto changed = message(); changed.timestamp = value;
        rejects([&] { (void)encodeJson(changed); });
    }
    for (const auto* value : {"", "has space", "bad\nid"}) {
        auto changed = message(); changed.sessionId = value;
        rejects([&] { (void)encodeJson(changed); });
    }
    rejects([&] { (void)decodeJson("[]"); });
    rejects([&] { (void)decodeJson("{"); });
    rejects([&] { (void)decodeJson(json + "garbage"); });
    rejects([&] { (void)decodeJson(json + QByteArray(1, static_cast<char>(0xff))); });
    rejects([&] { (void)decodeJson("{\"messageId\":\"other\"," + json.mid(1)); });
    rejects([&] { (void)decodeJson("{\"message\\u0049d\":\"other\"," + json.mid(1)); });
    rejects([&] { (void)decodeJson(QByteArray{}); });
    rejects([&] { (void)encodeJson(message("unknown")); });
    rejects([&] { (void)encodeJson(message("heartbeat", {{"kind", "pong"}})); });
    rejects([&] { (void)encodeJson(message("heartbeat", {{"kind", "ping"}, {"inReplyTo", "m"}})); });
    rejects([&] { (void)encodeJson(message("registration_result", {{"accepted", false}, {"inReplyTo", "m"}})); });
    rejects([&] { (void)encodeJson(message("approval_response", {{"requestId", "r"}, {"decision", "yes"}})); });
    for (const auto* value : {"-1", "01", "18446744073709551616", "1.5", " 1"}) {
        rejects([&] { (void)encodeJson(message("event", {{"eventType", "TaskStarted"}, {"sequence", value}})); });
    }
    rejects([&] { (void)encodeJson(message("event", {{"eventType", "TaskStarted"}, {"sequence", 1}})); });
    rejects([&] { (void)encodeJson(message("event", {{"eventType", "TaskStarted"}})); });
    rejects([&] { (void)encodeJson(message("event", {{"eventType", "ApprovalRequested"}, {"sequence", "0"}})); });
    rejects([&] { (void)encodeJson(message("event", {{"eventType", "unknown"}, {"sequence", "0"}})); });
    rejects([&] { (void)encodeJson(message("error", {{"code", "x"}, {"message", QString(2049, 'a')}})); });
}
void framingLimits()
{
    const auto body = encodeJson(message());
    const auto padded = body + QByteArray(maxMessageBytes - body.size(), ' ');
    FrameDecoder maximum;
    check(maximum.feed(rawFrame(padded)).size() == 1);
    maximum.finish();
    rejects([&] { (void)decodeJson(padded + ' '); });
    for (const quint32 length : {quint32{0}, quint32{65537}, quint32{0xffffffff}}) {
        FrameDecoder stream;
        QByteArray header(4, '\0');
        qToBigEndian(length, header.data());
        rejects([&] { (void)stream.feed(header); });
        check(stream.failed() && stream.bufferedBytes() == 0);
        rejects([&] { (void)stream.feed(encodeFrame(message())); });
    }
    for (const auto& partial : {QByteArray(1, '\0'), encodeFrame(message()).chopped(1)}) {
        FrameDecoder stream;
        check(stream.feed(partial).empty());
        rejects([&] { stream.finish(); });
        check(stream.failed());
    }
    FrameDecoder badJson;
    rejects([&] { (void)badJson.feed(rawFrame("garbage")); });
    check(badJson.failed());
    FrameDecoder largeRead;
    rejects([&] { (void)largeRead.feed(QByteArray(maxReadBytes + 1, ' ')); });
    FrameDecoder empty;
    check(empty.feed({}).empty());
    empty.finish();
}

void registrationValidation()
{
    const QJsonObject project{{"name", "Project"}, {"directory", "C:\\Projects\\One"}};
    const QJsonObject color{{"red", 0}, {"green", 0}, {"blue", 0}};
    const QJsonObject pet{{"id", "p1"}, {"name", "Penny"}, {"appearanceKey", "round"}, {"color", color}};
    const QJsonObject payload{{"project", project}, {"pet", pet}, {"terminalType", "Terminal"}};
    check(decodeJson(encodeJson(message("register", payload))).payload == payload);
    for (const auto* field : {"project", "pet", "terminalType"}) {
        auto bad = payload; bad.remove(field);
        rejects([&] { (void)encodeJson(message("register", bad)); });
    }
    for (const auto* path : {"relative", "C:relative", "\\rooted", "\\\\server", ""}) {
        auto badProject = project; badProject.insert("directory", path);
        auto bad = payload; bad.insert("project", badProject);
        rejects([&] { (void)encodeJson(message("register", bad)); });
    }
    auto uncProject = project; uncProject.insert("directory", "\\\\server\\share\\project");
    auto unc = payload; unc.insert("project", uncProject);
    check(!encodeJson(message("register", unc)).isEmpty());
    for (const auto pid : {0.0, -1.0, 1.5, 4294967296.0}) {
        auto bad = payload; bad.insert("processId", pid);
        rejects([&] { (void)encodeJson(message("register", bad)); });
    }
    for (const auto channel : {-1.0, 0.5, 256.0}) {
        auto badColor = color; badColor.insert("red", channel);
        auto badPet = pet; badPet.insert("color", badColor);
        auto bad = payload; bad.insert("pet", badPet);
        rejects([&] { (void)encodeJson(message("register", bad)); });
    }
    auto extra = payload; extra.insert("unexpected", true);
    rejects([&] { (void)encodeJson(message("register", extra)); });
    auto nested = pet; nested.insert("unexpected", true);
    auto bad = payload; bad.insert("pet", nested);
    rejects([&] { (void)encodeJson(message("register", bad)); });
    auto invalidUtf16 = payload;
    invalidUtf16.insert("terminalType", QString(1, QChar{0xd800}));
    rejects([&] { (void)encodeJson(message("register", invalidUtf16)); });
}
}
int main()
{
    try {
        roundTrips(); invalidMessages(); framingLimits(); registrationValidation();
        std::cout << checks << " IPC checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
