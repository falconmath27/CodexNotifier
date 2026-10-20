#include "dimepenny/persistence/sqlite_store.h"
#include "dimepenny/application/event_processor.h"
#include "dimepenny/ipc/simulated_bridge.h"
#include <QDateTime>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

using namespace dimepenny;
namespace {
class InspectDb {
    QString name_ = QUuid::createUuid().toString();
public:
    QSqlDatabase db;
    explicit InspectDb(const QString& path) {
        db = QSqlDatabase::addDatabase("QSQLITE", name_); db.setDatabaseName(path);
        if (!db.open()) throw std::runtime_error("Test DB failed to open");
    }
    ~InspectDb() { db.close(); db = {}; QSqlDatabase::removeDatabase(name_); }
    QVariant scalar(const QString& sql) {
        QSqlQuery q(db);
        if (!q.exec(sql) || !q.next()) throw std::runtime_error("Test query failed");
        return q.value(0);
    }
    bool exec(const QString& sql) { QSqlQuery q(db); return q.exec(sql); }
};
Session model() {
    return {SessionId{"saved"}, ProjectInfo{"Project's Unicode", std::filesystem::path{L"C:/Projects/Example"}},
        PetIdentity{"pet", "Dime", "orange", {255,111,44}}, "PowerShell", Session::Timestamp{std::chrono::seconds{1}}};
}
ipc::Message eventMessage(const QString& type, quint64 sequence, QJsonObject approval = {}) {
    QJsonObject payload{{"eventType", type}, {"sequence", QString::number(sequence)}};
    if (!approval.isEmpty()) payload.insert("approval", approval);
    return {ipc::protocolVersion, "event-" + QString::number(sequence), "saved",
        QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(sequence + 10) * 1000, QTimeZone::UTC).toString(Qt::ISODateWithMs), "event", payload};
}
}
class PersistenceTest : public QObject {
    Q_OBJECT
private slots:
    void roundTripAndMigration() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("state.sqlite3");
        auto s = model();
        QCOMPARE(AgentStateMachine::beginConnecting(s), TransitionResult::Applied);
        const ipc::Message registration{ipc::protocolVersion, "registration", "saved", "2026-01-01T00:00:00.000Z", "register", {{"authToken", "never-save-this-secret"}}};
        {
            SqliteStore store(path);
            QVERIFY_EXCEPTION_THROWN(SqliteStore secondWriter(path), StorageError);
            store.save(s, true, &registration);
            store.setSetting("quoted'key", "Unicode ✓");
            auto apply = [&](AgentEventType type, const ipc::Message& message, std::optional<ApprovalInfo> approval = std::nullopt) {
                AgentEvent event{message.messageId.toStdString(), s.id(), type,
                    Session::Timestamp{std::chrono::milliseconds{QDateTime::fromString(message.timestamp, Qt::ISODateWithMs).toMSecsSinceEpoch()}},
                    message.payload.value("sequence").toString().toULongLong(), approval};
                QCOMPARE(AgentStateMachine::apply(s, event), TransitionResult::Applied);
                store.save(s, true, &message);
            };
            apply(AgentEventType::SessionConnected, eventMessage("SessionConnected", 0));
            apply(AgentEventType::TaskStarted, eventMessage("TaskStarted", 1));
            apply(AgentEventType::ApprovalRequested, eventMessage("ApprovalRequested", 2, {{"requestId", "request"}}), ApprovalInfo{"request", std::nullopt});
            apply(AgentEventType::ApprovalResolved, eventMessage("ApprovalResolved", 3, {{"requestId", "request"}, {"decision", "approve"}}), ApprovalInfo{"request", ApprovalDecision::Approve});
            const auto duplicate = eventMessage("ApprovalResolved", 3, {{"requestId", "request"}, {"decision", "approve"}});
            QVERIFY_EXCEPTION_THROWN(store.save(s, false, &duplicate), StorageError);
            InspectDb inspect(path);
            QCOMPARE(inspect.scalar("SELECT enabled FROM sessions").toInt(), 1); // failed history insert rolled back snapshot
            QCOMPARE(inspect.scalar("SELECT count(*) FROM events").toInt(), 4);
            QCOMPARE(inspect.scalar("SELECT count(*) FROM registrations").toInt(), 1);
            QCOMPARE(inspect.scalar("SELECT count(*) FROM approval_history").toInt(), 2);
            QCOMPARE(inspect.scalar("SELECT count(*) FROM projects").toInt(), 1);
            QCOMPARE(inspect.scalar("SELECT count(*) FROM pet_identities").toInt(), 1);
        }
        {
            SqliteStore store(path);
            auto saved = store.loadSessions(); QCOMPARE(saved.size(), std::size_t{1});
            auto& restored = saved.front().session;
            QVERIFY(restored.state() == AgentState::Disconnected);
            QVERIFY(restored.pet() == s.pet()); QVERIFY(restored.project() == s.project());
            QCOMPARE(restored.createdAt(), s.createdAt());
            QCOMPARE(restored.lastSequence(), s.lastSequence());
            QCOMPARE(store.setting("quoted'key"), QString::fromUtf8("Unicode ✓"));
            QCOMPARE(store.setting("missing", "fallback"), QString("fallback"));
            const AgentEvent replay{"event-3", restored.id(), AgentEventType::TaskStarted, std::chrono::system_clock::now(), 3};
            QCOMPARE(AgentStateMachine::apply(restored, replay), TransitionResult::DuplicateEvent);
            QCOMPARE(AgentStateMachine::beginConnecting(restored), TransitionResult::Applied);
            const AgentEvent stale{"new-id", restored.id(), AgentEventType::SessionConnected, std::chrono::system_clock::now(), 2};
            QCOMPARE(AgentStateMachine::apply(restored, stale), TransitionResult::OutOfOrder);
        }
        // Simulate the previous schema with real rows; v2 backfills approval history.
        {
            InspectDb inspect(path);
            QVERIFY(inspect.exec("DROP TABLE approval_history"));
            QVERIFY(inspect.exec("PRAGMA user_version=1"));
            QVERIFY(inspect.exec("CREATE TABLE approval_history (unexpected TEXT)"));
        }
        QVERIFY_EXCEPTION_THROWN(SqliteStore brokenUpgrade(path), StorageError);
        {
            InspectDb inspect(path);
            QCOMPARE(inspect.scalar("PRAGMA user_version").toInt(), 1);
            QCOMPARE(inspect.scalar("SELECT count(*) FROM events").toInt(), 4);
            QVERIFY(inspect.exec("DROP TABLE approval_history"));
        }
        { SqliteStore upgraded(path); QCOMPARE(upgraded.loadSessions().size(), std::size_t{1}); }
        {
            InspectDb inspect(path);
            QCOMPARE(inspect.scalar("PRAGMA user_version").toInt(), 2);
            QCOMPARE(inspect.scalar("SELECT count(*) FROM approval_history").toInt(), 2);
            QVERIFY(inspect.exec("PRAGMA user_version=99"));
        }
        QVERIFY_EXCEPTION_THROWN(SqliteStore future(path), StorageError);
        InspectDb inspect(path);
        QCOMPARE(inspect.scalar("PRAGMA user_version").toInt(), 99);
        QCOMPARE(inspect.scalar("SELECT count(*) FROM events").toInt(), 4);
    }
    void processorCommitAndRestart() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("processor.sqlite3");
        const auto endpoint = "persistence-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            EventProcessor processor; processor.enablePersistence(path);
            QVERIFY(processor.server().listen(endpoint));
            ipc::SimulatedBridge bridge(endpoint, "saved", processor.server().authorize("saved"), SqliteStore::registrationPayload(model()));
            bridge.connectToServer(); QTRY_VERIFY(bridge.isReady());
            QVERIFY(bridge.sendEvent("SessionConnected"));
            QTRY_VERIFY(processor.registry().find(SessionId{"saved"})->state() == AgentState::Active);
            InspectDb inspect(path);
            QVERIFY(inspect.exec("CREATE TRIGGER fail_event BEFORE INSERT ON events BEGIN SELECT RAISE(ABORT,'injected failure'); END"));
            QSignalSpy errors(&processor, &EventProcessor::persistenceFailed);
            QVERIFY(bridge.sendEvent("TaskStarted"));
            QTRY_COMPARE(errors.count(), 1);
            QVERIFY(processor.registry().find(SessionId{"saved"})->state() == AgentState::Active);
            QCOMPARE(inspect.scalar("SELECT count(*) FROM events").toInt(), 1);
            QVERIFY(inspect.exec("DROP TRIGGER fail_event"));
            QVERIFY(bridge.replayLastEvent());
            QTRY_VERIFY(processor.registry().find(SessionId{"saved"})->state() == AgentState::Working);
            QCOMPARE(inspect.scalar("SELECT count(*) FROM events").toInt(), 2);
            processor.server().stop();
        }
        {
            EventProcessor processor; processor.enablePersistence(path);
            const auto* saved = processor.registry().find(SessionId{"saved"}); QVERIFY(saved);
            QVERIFY(saved->state() == AgentState::Disconnected);
            QVERIFY(processor.server().listen(endpoint));
            ipc::SimulatedBridge bridge(endpoint, "saved", processor.server().authorize("saved"), SqliteStore::registrationPayload(*saved));
            bridge.resumeAfter(*saved->lastSequence());
            bridge.connectToServer(); QTRY_VERIFY(bridge.isReady());
            QVERIFY(bridge.sendEvent("SessionConnected"));
            QTRY_VERIFY(processor.registry().find(SessionId{"saved"})->state() == AgentState::Active);
            InspectDb inspect(path);
            QCOMPARE(inspect.scalar("SELECT count(*) FROM registrations").toInt(), 2);
            QCOMPARE(inspect.scalar("SELECT count(*) FROM events").toInt(), 3);
        }
    }
    void invalidPath() {
        QTemporaryDir directory;
        QVERIFY_EXCEPTION_THROWN(SqliteStore store(directory.filePath("missing/state.sqlite3")), StorageError);
    }
    void inactiveAndBrokenReferences() {
        QTemporaryDir directory;
        const auto path = directory.filePath("inactive.sqlite3");
        {
            SqliteStore store(path);
            auto s = model(); AgentStateMachine::deactivate(s);
            store.save(s, false);
        }
        {
            EventProcessor processor; processor.enablePersistence(path);
            QCOMPARE(processor.registry().size(), std::size_t{1});
            QCOMPARE(processor.registry().activeCount(), std::size_t{0});
            QVERIFY(processor.registry().find(SessionId{"saved"})->state() == AgentState::Disconnected);
        }
        {
            InspectDb inspect(path); // Deliberately violate references via an external writer.
            QVERIFY(inspect.exec("DELETE FROM projects"));
        }
        EventProcessor processor;
        QVERIFY_EXCEPTION_THROWN(processor.enablePersistence(path), StorageError);
        QCOMPARE(processor.registry().size(), std::size_t{0});
    }
};
QTEST_GUILESS_MAIN(PersistenceTest)
#include "persistence_test.moc"
