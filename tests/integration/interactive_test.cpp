#include "activation_window.h"
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <vector>

using dimepenny::ActivationWindow;
namespace {
QString endpoint() { return "dimepenny-test-interactive-" + QUuid::createUuid().toString(QUuid::Id128); }
QJsonObject receive(QLocalSocket& socket)
{
    QByteArray bytes;
    const bool ready = QTest::qWaitFor([&] { bytes += socket.readAll(); return bytes.contains('\n'); }, 6500);
    return ready ? QJsonDocument::fromJson(bytes.left(bytes.indexOf('\n'))).object() : QJsonObject{};
}
void send(QLocalSocket& socket, const QJsonObject& object)
{
    socket.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
}
QJsonObject attach(QLocalSocket& socket, const QString& name, const QString& project, const QString& id = {})
{
    socket.connectToServer(name);
    if (!socket.waitForConnected(1000)) return {};
    send(socket, {{"v", 1}, {"op", "attach"}, {"project", project}, {"sessionId", id}});
    return receive(socket);
}
QJsonObject hook(const QString& name, const QJsonObject& registration, const QString& project,
                 const QString& event, const QString& codex, const QString& turn = {})
{
    QLocalSocket socket; socket.connectToServer(name);
    if (!socket.waitForConnected(1000)) return {};
    send(socket, {{"v", 1}, {"op", "hook"}, {"token", registration.value("token")},
        {"event", QJsonObject{{"hook_event_name", event}, {"session_id", codex}, {"cwd", project}, {"turn_id", turn}}}});
    return receive(socket);
}
QString state(const ActivationWindow& window, const QJsonObject& registration)
{
    for (const auto& item : window.sessions()) {
        const auto row = item.toMap();
        if (row.value("sessionId").toString() == registration.value("sessionId").toString()) return row.value("state").toString();
    }
    return {};
}
QString helper() { return QCoreApplication::applicationDirPath() + "/../bridge/DimePennyInteractive.exe"; }
QJsonObject lease(QProcess& process, const QString& name, const QString& directory)
{
    process.start(helper(), {"--attach", QString::number(QCoreApplication::applicationPid()), "--endpoint", name});
    if (!process.waitForStarted()) return {};
    process.write(QJsonDocument(QJsonObject{{"project", directory}}).toJson(QJsonDocument::Compact));
    process.closeWriteChannel();
    QByteArray bytes;
    if (!QTest::qWaitFor([&] { bytes += process.readAllStandardOutput(); return bytes.contains('\n'); }, 7000)) return {};
    return QJsonDocument::fromJson(bytes).object();
}
}
class InteractiveTest : public QObject {
    Q_OBJECT
private slots:
    void killedTerminalDisconnectsOnlyItsPet()
    {
        QTemporaryDir directory; ActivationWindow window; const auto name = endpoint(); QVERIFY(window.enableInteractive(name));
        QProcess survivor; const auto kept = lease(survivor, name, directory.path()); QVERIFY(kept.value("ok").toBool());
        QProcess terminal; terminal.setWorkingDirectory(directory.path());
        const auto root = QCoreApplication::applicationDirPath() + "/../..";
        terminal.start("powershell.exe", {"-NoProfile", "-NonInteractive", "-File", root + "/tools/Start-DimePennyCodex.ps1",
            "-BridgePath", helper(), "-CodexCommand", QCoreApplication::applicationDirPath() + "/dimepenny_fake_interactive_codex.exe",
            "-Endpoint", name, "-CodexArguments", directory.path()});
        QVERIFY(terminal.waitForStarted());
        QByteArray output;
        QVERIFY(QTest::qWaitFor([&] { output += terminal.readAllStandardOutput(); return output.contains("FAKE_CONSOLE_READY") || terminal.state() == QProcess::NotRunning; }, 10000));
        QVERIFY2(output.contains("FAKE_CONSOLE_READY"), terminal.readAllStandardError().constData());
        QTRY_COMPARE(window.sessions().size(), 2);
        terminal.kill(); terminal.waitForFinished();
        QTRY_VERIFY([&] {
            for (const auto& entry : window.sessions()) {
                const auto row = entry.toMap();
                if (row.value("sessionId").toString() != kept.value("sessionId").toString())
                    return row.value("state").toString() == "Disconnected";
            }
            return false;
        }());
        QCOMPARE(survivor.state(), QProcess::Running);
        QCOMPARE(state(window, kept), "Connecting"); // No actual SessionStart was fabricated.
        survivor.kill(); survivor.waitForFinished();
    }
    void powershellLauncher()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        ActivationWindow window; const auto name = endpoint(); QVERIFY(window.enableInteractive(name));
        QProcess shell; shell.setWorkingDirectory(directory.path());
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("DIMEPENNY_HOOK_ENDPOINT", "previous-endpoint");
        environment.insert("DIMEPENNY_HOOK_TOKEN", "previous-token");
        shell.setProcessEnvironment(environment);
        const auto quote = [](QString value) { return "'" + value.replace("'", "''") + "'"; };
        const auto root = QCoreApplication::applicationDirPath() + "/../..";
        const auto script = "& " + quote(root + "/tools/Start-DimePennyCodex.ps1") + " -BridgePath " + quote(helper())
            + " -CodexCommand " + quote(QCoreApplication::applicationDirPath() + "/dimepenny_fake_interactive_codex.exe")
            + " -Endpoint " + quote(name) + " -CodexArguments " + quote(directory.path())
            + "; if ($LASTEXITCODE -ne 23 -or $env:DIMEPENNY_HOOK_TOKEN -ne 'previous-token'"
              " -or $env:DIMEPENNY_HOOK_ENDPOINT -ne 'previous-endpoint') { throw 'Launcher cleanup failed' }";
        QSet<QString> observed;
        connect(&window, &ActivationWindow::changed, this, [&] {
            for (const auto& entry : window.sessions()) observed.insert(entry.toMap().value("state").toString());
        });
        shell.start("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command", script});
        QVERIFY(shell.waitForStarted());
        QByteArray output;
        QVERIFY(QTest::qWaitFor([&] { output += shell.readAllStandardOutput(); return output.contains("FAKE_CONSOLE_READY") || shell.state() == QProcess::NotRunning; }, 10000));
        QVERIFY2(output.contains("FAKE_CONSOLE_READY"), shell.readAllStandardError().constData());
        shell.write("interactive input\n"); shell.closeWriteChannel();
        const auto finished = QTest::qWaitFor([&] { return shell.state() == QProcess::NotRunning; }, 15000);
        output += shell.readAllStandardOutput(); const auto errors = shell.readAllStandardError();
        QVERIFY2(finished, (output + errors).constData());
        QVERIFY2(shell.exitCode() == 0, errors.constData());
        QVERIFY(output.contains("FAKE_CONSOLE_FINISHED"));
        QTRY_COMPARE(window.sessions().size(), 1);
        QTRY_COMPARE(window.sessions().first().toMap().value("state").toString(), "Disconnected");
        QVERIFY(observed.contains("Active")); QVERIFY(observed.contains("Working")); QVERIFY(observed.contains("Completed"));
    }
    void independentTerminalsAndValidation()
    {
        QTemporaryDir directory, other; QVERIFY(directory.isValid()); QVERIFY(other.isValid());
        ActivationWindow window;
        const auto name = endpoint(); QVERIFY(window.enableInteractive(name));
        QLocalSocket first, second;
        const auto a = attach(first, name, directory.path());
        const auto b = attach(second, name, directory.path());
        QVERIFY(a.value("ok").toBool()); QVERIFY(b.value("ok").toBool());
        QVERIFY(a.value("sessionId") != b.value("sessionId")); QVERIFY(a.value("token") != b.value("token"));
        QTRY_COMPARE(state(window, a), "Connecting");
        QVERIFY(!hook(name, a, directory.path(), "UserPromptSubmit", "codex-a", "turn-1").value("ok").toBool());
        QVERIFY(hook(name, a, directory.path(), "SessionStart", "codex-a").value("ok").toBool());
        QVERIFY(!hook(name, b, directory.path(), "SessionStart", "codex-a").value("ok").toBool());
        QVERIFY(hook(name, b, directory.path(), "SessionStart", "codex-b").value("ok").toBool());
        QTRY_COMPARE(state(window, a), "Active"); QTRY_COMPARE(state(window, b), "Active");
        QVERIFY(!hook(name, a, other.path(), "UserPromptSubmit", "codex-a", "turn-1").value("ok").toBool());
        QVERIFY(!hook(name, a, directory.path(), "UserPromptSubmit", "codex-b", "turn-1").value("ok").toBool());
        auto wrongToken = a; wrongToken.insert("token", "wrong");
        QVERIFY(!hook(name, wrongToken, directory.path(), "UserPromptSubmit", "codex-a", "turn-1").value("ok").toBool());
        QVERIFY(hook(name, a, directory.path(), "UserPromptSubmit", "codex-a", "turn-1").value("ok").toBool());
        QVERIFY(hook(name, a, directory.path(), "UserPromptSubmit", "codex-a", "turn-1").value("ok").toBool());
        QVERIFY(hook(name, a, directory.path(), "SessionStart", "codex-a").value("ok").toBool());
        QVERIFY(!hook(name, a, directory.path(), "Stop", "codex-a", "wrong-turn").value("ok").toBool());
        QTRY_COMPARE(state(window, a), "Working"); QCOMPARE(state(window, b), "Active");
        QVERIFY(hook(name, a, directory.path(), "Interrupt", "codex-a", "turn-1").value("ok").toBool());
        QTRY_COMPARE(state(window, a), "Active");
        QVERIFY(!hook(name, a, directory.path(), "UserPromptSubmit", "codex-a", "turn-1").value("ok").toBool());
        QVERIFY(hook(name, a, directory.path(), "UserPromptSubmit", "codex-a", "turn-2").value("ok").toBool());
        QVERIFY(hook(name, a, directory.path(), "Stop", "codex-a", "turn-2").value("ok").toBool());
        QVERIFY(hook(name, a, directory.path(), "Stop", "codex-a", "turn-2").value("ok").toBool());
        QTRY_COMPARE(state(window, a), "Completed");
        QLocalSocket duplicate;
        QVERIFY(!attach(duplicate, name, directory.path(), a.value("sessionId").toString()).value("ok").toBool());
        first.abort(); QTRY_COMPARE(state(window, a), "Disconnected"); QCOMPARE(state(window, b), "Active");
        QVERIFY(!hook(name, a, directory.path(), "SessionStart", "codex-a").value("ok").toBool());
        QVERIFY(hook(name, b, directory.path(), "SessionEnd", "codex-b").value("ok").toBool());
        QTRY_COMPARE(state(window, b), "Disconnected");
    }
    void capacityAndRecovery()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto database = directory.filePath("interactive.sqlite3");
        QJsonObject original;
        {
            ActivationWindow window(database); const auto name = endpoint(); QVERIFY(window.enableInteractive(name));
            std::vector<std::unique_ptr<QLocalSocket>> owners;
            for (int i = 0; i < 10; ++i) {
                owners.push_back(std::make_unique<QLocalSocket>());
                const auto registration = attach(*owners.back(), name, directory.path());
                QVERIFY(registration.value("ok").toBool()); if (i == 0) original = registration;
            }
            QTRY_COMPARE(window.sessions().size(), 10);
            QSet<QString> names;
            for (const auto& row : window.sessions()) names.insert(row.toMap().value("name").toString());
            QCOMPARE(names.size(), 10);
            QLocalSocket extra; QVERIFY(!attach(extra, name, directory.path()).value("ok").toBool());
            QVERIFY(hook(name, original, directory.path(), "SessionStart", "original").value("ok").toBool());
            QVERIFY(hook(name, original, directory.path(), "UserPromptSubmit", "original", "old-turn").value("ok").toBool());
            QTRY_COMPARE(state(window, original), "Working");
        }
        {
            ActivationWindow window(database); const auto name = endpoint(); QVERIFY(window.enableInteractive(name));
            QTRY_COMPARE(window.sessions().size(), 10);
            for (const auto& row : window.sessions()) QCOMPARE(row.toMap().value("state").toString(), "Disconnected");
            QVERIFY(!hook(name, original, directory.path(), "SessionStart", "original").value("ok").toBool());
            QLocalSocket owner;
            const auto fresh = attach(owner, name, directory.path(), original.value("sessionId").toString());
            QVERIFY(fresh.value("ok").toBool()); QCOMPARE(fresh.value("sessionId"), original.value("sessionId"));
            QVERIFY(fresh.value("token") != original.value("token"));
            QTRY_COMPARE(state(window, fresh), "Connecting");
            QVERIFY(!hook(name, fresh, directory.path(), "SessionStart", "different-thread").value("ok").toBool());
            QVERIFY(hook(name, fresh, directory.path(), "SessionStart", "original").value("ok").toBool());
            QVERIFY(hook(name, fresh, directory.path(), "UserPromptSubmit", "original", "new-turn").value("ok").toBool());
            QVERIFY(hook(name, fresh, directory.path(), "Stop", "original", "new-turn").value("ok").toBool());
            QTRY_COMPARE(state(window, fresh), "Completed");
            QCOMPARE(window.capacityUsed(), 10);
        }
    }
    void timeoutAndMalformedMessages()
    {
        QTemporaryDir directory; ActivationWindow window; const auto name = endpoint(); QVERIFY(window.enableInteractive(name));
        QLocalSocket idle; const auto registration = attach(idle, name, directory.path()); QVERIFY(registration.value("ok").toBool());
        QLocalSocket malformed; malformed.connectToServer(name); QVERIFY(malformed.waitForConnected(1000));
        malformed.write("{bad json}\n"); malformed.flush(); QVERIFY(!receive(malformed).value("ok").toBool());
        QLocalSocket version; version.connectToServer(name); QVERIFY(version.waitForConnected(1000));
        send(version, {{"v", 2}, {"op", "attach"}, {"project", directory.path()}}); QVERIFY(!receive(version).value("ok").toBool());
        QLocalSocket oversize; oversize.connectToServer(name); QVERIFY(oversize.waitForConnected(1000));
        oversize.write(QByteArray(65537, 'x')); oversize.flush();
        QTRY_COMPARE(oversize.state(), QLocalSocket::UnconnectedState);
        QTRY_COMPARE_WITH_TIMEOUT(state(window, registration), "Disconnected", 12000);
    }
    void nativeLeaseAndPassiveHook()
    {
        QTemporaryDir directory; ActivationWindow window; const auto name = endpoint(); QVERIFY(window.enableInteractive(name));
        QProcess owner;
        const auto registration = lease(owner, name, directory.path()); QVERIFY(registration.value("ok").toBool());
        QCOMPARE(registration.value("endpoint").toString(), name);
        QProcess hookProcess;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("DIMEPENNY_HOOK_ENDPOINT", name);
        environment.insert("DIMEPENNY_HOOK_TOKEN", registration.value("token").toString());
        hookProcess.setProcessEnvironment(environment);
        hookProcess.start(helper(), {"--hook"}); QVERIFY(hookProcess.waitForStarted());
        hookProcess.write(QJsonDocument(QJsonObject{{"session_id", "native"}, {"cwd", directory.path()},
            {"hook_event_name", "SessionStart"}, {"prompt", "PRIVATE DO NOT STORE"}, {"transcript_path", "DO NOT READ"}}).toJson());
        hookProcess.closeWriteChannel();
        QTRY_COMPARE(hookProcess.state(), QProcess::NotRunning); QCOMPARE(hookProcess.exitCode(), 0);
        QCOMPARE(hookProcess.readAllStandardOutput().trimmed(), QByteArray("{}"));
        QTRY_COMPARE(state(window, registration), "Active");
        // Execute the generated Windows command too: quoting must survive '&'
        // in the checkout path and hook stdin must reach the native helper.
        QProcess config; config.start(helper(), {"--print-hooks"}); QVERIFY(config.waitForFinished());
        const auto configObject = QJsonDocument::fromJson(config.readAllStandardOutput()).object();
        const auto command = configObject.value("hooks").toObject().value("UserPromptSubmit").toArray().first()
            .toObject().value("hooks").toArray().first().toObject().value("command").toString();
        QVERIFY(!command.isEmpty());
        hookProcess.start("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command", command});
        QVERIFY(hookProcess.waitForStarted());
        hookProcess.write(QJsonDocument(QJsonObject{{"session_id", "native"}, {"cwd", directory.path()},
            {"hook_event_name", "UserPromptSubmit"}, {"turn_id", "native-turn"}}).toJson());
        hookProcess.closeWriteChannel();
        QTRY_COMPARE_WITH_TIMEOUT(hookProcess.state(), QProcess::NotRunning, 10000);
        QCOMPARE(hookProcess.exitCode(), 0);
        QCOMPARE(hookProcess.readAllStandardOutput().trimmed(), QByteArray("{}"));
        QTRY_COMPARE(state(window, registration), "Working");
        QTest::qWait(2200); QCOMPARE(owner.state(), QProcess::Running); // Heartbeat, not terminal activity.
        owner.kill(); owner.waitForFinished();
        QTRY_COMPARE(state(window, registration), "Disconnected");
        hookProcess.start(helper(), {"--hook"}); QVERIFY(hookProcess.waitForStarted());
        hookProcess.write("{}"); hookProcess.closeWriteChannel();
        QTRY_COMPARE(hookProcess.state(), QProcess::NotRunning); QCOMPARE(hookProcess.exitCode(), 0);
        QCOMPARE(hookProcess.readAllStandardOutput().trimmed(), QByteArray("{}"));
    }
};
QTEST_MAIN(InteractiveTest)
#include "interactive_test.moc"
