#include "dimepenny/integration/codex_exec.h"
#include <QTemporaryDir>
#include <QtTest>
using namespace dimepenny;
class CodexExecTest : public QObject {
    Q_OBJECT
private slots:
    void structuredLifecycle() {
        CodexLifecycle decoder;
        QSignalSpy events(&decoder, &CodexLifecycle::lifecycle);
        const QByteArray stream = "{\"type\":\"thread.started\",\"thread_id\":\"t1\"}\n"
            "{\"type\":\"turn.started\"}\r\n"
            "{\"type\":\"turn.started\"}\n"
            "{\"type\":\"item.completed\",\"item\":{\"text\":\"Error Approval needed\"}}\n"
            "{\"type\":\"error\",\"message\":\"retrying\"}\n"
            "{\"type\":\"future.event\"}\n"
            "{\"type\":\"turn.completed\"}\n"
            "{\"type\":\"turn.completed\"}";
        for (char c : stream) decoder.consume(QByteArray(1, c));
        decoder.finish(true);
        QCOMPARE(events.size(), 3);
        QCOMPARE(events[0][0].toString(), QString("SessionConnected"));
        QCOMPARE(events[1][0].toString(), QString("TaskStarted"));
        QCOMPARE(events[2][0].toString(), QString("TaskCompleted"));
        QCOMPARE(decoder.threadId(), QString("t1"));
        QVERIFY(!decoder.failed());
        decoder.consume("{\"type\":\"turn.failed\"}\n");
        QCOMPARE(events.size(), 3);
    }
    void invalidStreams_data() {
        QTest::addColumn<QByteArray>("stream");
        QTest::newRow("plain text") << QByteArray("Working\n");
        QTest::newRow("missing type") << QByteArray("{}\n");
        QTest::newRow("missing identity") << QByteArray("{\"type\":\"thread.started\"}\n");
        QTest::newRow("early turn") << QByteArray("{\"type\":\"turn.started\"}\n");
        QTest::newRow("early completion") << QByteArray("{\"type\":\"turn.completed\"}\n");
        QTest::newRow("identity switch") << QByteArray("{\"type\":\"thread.started\",\"thread_id\":\"a\"}\n{\"type\":\"thread.started\",\"thread_id\":\"b\"}\n");
        QTest::newRow("failed turn") << QByteArray("{\"type\":\"turn.failed\"}\n");
        QTest::newRow("truncated") << QByteArray("{\"type\":");
        QTest::newRow("oversize") << QByteArray(1024 * 1024 + 1, 'a');
        QTest::newRow("no lifecycle") << QByteArray("{\"type\":\"item.completed\"}\n");
    }
    void invalidStreams() {
        QFETCH(QByteArray, stream);
        CodexLifecycle decoder; QSignalSpy events(&decoder, &CodexLifecycle::lifecycle);
        decoder.consume(stream); decoder.finish(true); decoder.finish(false);
        QVERIFY(decoder.failed());
        QCOMPARE(events.last()[0].toString(), QString("SessionError"));
        int errors = 0; for (const auto& event : events) if (event[0] == "SessionError") ++errors;
        QCOMPARE(errors, 1);
    }
    void process_data() {
        QTest::addColumn<QString>("prompt"); QTest::addColumn<bool>("success");
        QTest::newRow("completed") << QString("test prompt & special characters") << true;
        QTest::newRow("failed") << QString("failed") << false;
        QTest::newRow("stderr is not lifecycle") << QString("empty") << false;
        QTest::newRow("nonzero") << QString("nonzero") << false;
    }
    void process() {
        QFETCH(QString, prompt); QFETCH(bool, success);
        QTemporaryDir project;
        CodexExec adapter;
        QSignalSpy done(&adapter, &CodexExec::finished), events(&adapter, &CodexExec::lifecycle);
        const auto binary = QCoreApplication::applicationDirPath() + "/dimepenny_fake_codex.exe";
        adapter.start(binary, project.path(), prompt);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done[0][0].toBool(), success);
        QCOMPARE(events.last()[0].toString(), success ? QString("TaskCompleted") : QString("SessionError"));
        if (success) QCOMPARE(adapter.threadId(), QString("fixture-thread"));
    }
    void missingExecutable() {
        QTemporaryDir project; CodexExec adapter;
        QSignalSpy done(&adapter, &CodexExec::finished);
        adapter.start(project.filePath("missing.exe"), project.path(), "prompt");
        QCOMPARE(done.size(), 1); QVERIFY(!done[0][0].toBool());
    }
};
QTEST_GUILESS_MAIN(CodexExecTest)
#include "codex_exec_test.moc"
