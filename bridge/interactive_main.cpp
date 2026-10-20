#include "dimepenny/integration/interactive_server.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDeadlineTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <cstdio>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
void output(const QJsonObject& object)
{
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    std::fflush(stdout);
}
QJsonObject input(qsizetype limit)
{
    // A PowerShell native-command pipeline may keep its stdin handle open.
    // Stop at one complete JSON object, not EOF. Respect braces inside strings.
    QByteArray bytes;
    int depth = 0;
    bool quoted = false, escaped = false, started = false;
    while (bytes.size() < limit) {
        const auto value = std::fgetc(stdin);
        if (value == EOF) break;
        const auto ch = static_cast<char>(value);
        bytes.append(ch);
        if (!started) {
            if (ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t') continue;
            if (ch != '{') return {};
            started = true; depth = 1; continue;
        }
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
        } else {
            if (ch == '"') quoted = true;
            else if (ch == '{' || ch == '[') ++depth;
            else if (ch == '}' || ch == ']') {
                if (--depth == 0) return QJsonDocument::fromJson(bytes).object();
            }
        }
    }
    return {};
}
QJsonObject exchange(QLocalSocket& socket, const QJsonObject& request, int timeout)
{
    QDeadlineTimer deadline(timeout);
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForBytesWritten(static_cast<int>(deadline.remainingTime()))) return {};
    QByteArray bytes;
    while (!deadline.hasExpired()) {
        bytes += socket.readAll();
        if (bytes.size() > 65536) return {};
        const auto end = bytes.indexOf('\n');
        if (end >= 0) return QJsonDocument::fromJson(bytes.left(end)).object();
        if (!socket.waitForReadyRead(static_cast<int>(deadline.remainingTime()))) return {};
    }
    return {};
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"hook", "Read one Codex hook JSON object from stdin; always return a passive response."});
    parser.addOption({"print-hooks", "Print opt-in hooks.json configuration for this executable; never install or trust it."});
    parser.addOption({"attach", "Maintain a terminal lease until this parent process exits. Registration JSON is read from stdin.", "parent-pid"});
    parser.addOption({"endpoint", "Override the local endpoint (testing).", "name", dimepenny::InteractiveServer::defaultEndpoint()});
    parser.process(app);
    if (parser.isSet("print-hooks")) {
        auto path = QCoreApplication::applicationFilePath();
        path.replace("'", "''");
        const auto script = "& '" + path + "' --hook";
        QByteArray utf16;
        for (const auto ch : script) {
            utf16.append(static_cast<char>(ch.unicode() & 0xff));
            utf16.append(static_cast<char>(ch.unicode() >> 8));
        }
        const auto command = "powershell.exe -NoLogo -NoProfile -NonInteractive -EncodedCommand " + QString::fromLatin1(utf16.toBase64());
        QJsonObject hooks;
        for (const auto* event : {"SessionStart", "UserPromptSubmit", "Stop", "Interrupt", "SessionEnd"})
            hooks.insert(event, QJsonArray{QJsonObject{{"hooks", QJsonArray{QJsonObject{
                {"type", "command"}, {"command", command}, {"timeout", 3}}}}}});
        output({{"description", "Dime&Penny passive interactive lifecycle (no approval decisions)"}, {"hooks", hooks}});
        return 0;
    }
    if (parser.isSet("hook")) {
        // Ignore unrelated Codex launches. Never collect prompt, assistant text,
        // transcript path, tool inputs or credentials from the hook object.
        const auto endpoint = qEnvironmentVariable("DIMEPENNY_HOOK_ENDPOINT");
        const auto token = qEnvironmentVariable("DIMEPENNY_HOOK_TOKEN");
        if (!endpoint.isEmpty() && !token.isEmpty()) {
            const auto incoming = input(1024 * 1024);
            QJsonObject event;
            for (const auto* key : {"hook_event_name", "session_id", "cwd", "turn_id", "source"})
                if (incoming.contains(key)) event.insert(key, incoming.value(key));
            QLocalSocket socket;
            socket.setReadBufferSize(65537);
            socket.connectToServer(endpoint);
            bool delivered = false;
            if (socket.waitForConnected(500)) {
                const auto response = exchange(socket, {{"v", 1}, {"op", "hook"}, {"token", token}, {"event", event}}, 1000);
                delivered = response.value("ok").toBool();
            }
            if (!delivered) std::fputs("Dime&Penny: hook not delivered; pet status may be stale.\n", stderr);
        }
        output({}); // Fail open: never block, continue a turn or decide approval.
        return 0;
    }
    if (!parser.isSet("attach")) return 2;
#ifndef Q_OS_WIN
    output({{"ok", false}, {"error", "PowerShell terminal leases currently require Windows"}});
    return 2;
#else
    bool validPid = false;
    const auto pid = parser.value("attach").toUInt(&validPid);
    const auto parent = validPid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
    if (!parent) { output({{"ok", false}, {"error", "Cannot monitor parent terminal"}}); return 2; }
    struct Handle { HANDLE value; ~Handle() { CloseHandle(value); } } guard{parent};
    auto request = input(65536);
    request.insert("v", 1); request.insert("op", "attach");
    QLocalSocket socket;
    socket.setReadBufferSize(65537);
    socket.connectToServer(parser.value("endpoint"));
    if (!socket.waitForConnected(1500)) {
        output({{"ok", false}, {"error", "Start Dime&Penny before launching Codex"}}); return 1;
    }
    auto response = exchange(socket, request, 5000);
    if (!response.value("ok").toBool()) {
        output({{"ok", false}, {"error", response.value("error").toString("Registration timed out")}}); return 1;
    }
    response.insert("endpoint", parser.value("endpoint"));
    output(response); // Private redirected stdout consumed only by the launcher.
    QTimer heartbeat, parentCheck, deadline;
    deadline.setSingleShot(true); deadline.start(10000);
    QObject::connect(&deadline, &QTimer::timeout, &app, [&] { app.exit(1); });
    QObject::connect(&socket, &QLocalSocket::disconnected, &app, [&] { app.exit(1); });
    QByteArray buffer;
    QObject::connect(&socket, &QLocalSocket::readyRead, &app, [&] {
        buffer += socket.readAll();
        if (buffer.size() > 65536) { app.exit(1); return; }
        while (buffer.contains('\n')) {
            const auto end = buffer.indexOf('\n');
            const auto message = QJsonDocument::fromJson(buffer.left(end)).object();
            buffer.remove(0, end + 1);
            if (!message.value("ok").toBool()) { app.exit(1); return; }
            deadline.start(10000);
        }
    });
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
        if (socket.bytesToWrite() > 65536) { app.exit(1); return; }
        socket.write("{\"v\":1,\"op\":\"ping\"}\n");
    });
    QObject::connect(&parentCheck, &QTimer::timeout, &app, [&] {
        if (WaitForSingleObject(parent, 0) != WAIT_TIMEOUT) app.quit();
    });
    heartbeat.start(2000); parentCheck.start(500);
    return app.exec();
#endif
}
