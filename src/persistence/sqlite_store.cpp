#include "dimepenny/persistence/sqlite_store.h"
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <QFileInfo>

namespace dimepenny {
namespace {
QString text(const std::string& s) { return QString::fromStdString(s); }
QSqlQuery query(const QSqlDatabase& db, const QString& sql, const QVariantList& values = {})
{
    QSqlQuery q(db);
    if (!q.prepare(sql)) throw StorageError(q.lastError().text().toStdString());
    for (const auto& value : values) q.addBindValue(value);
    if (!q.exec()) throw StorageError(q.lastError().text().toStdString());
    return q;
}
class Transaction {
    QSqlDatabase db_;
    bool committed_ = false;
public:
    explicit Transaction(QSqlDatabase db) : db_(std::move(db)) {
        if (!db_.transaction()) throw StorageError(db_.lastError().text().toStdString());
    }
    ~Transaction() { if (!committed_) db_.rollback(); }
    void commit() {
        if (!db_.commit()) throw StorageError(db_.lastError().text().toStdString());
        committed_ = true;
    }
};
qlonglong ticks(Session::Timestamp t) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
}
Session::Timestamp timestamp(const QVariant& v) {
    bool ok = false;
    const auto value = v.toLongLong(&ok);
    if (!ok) throw StorageError("Invalid stored timestamp");
    return Session::Timestamp{std::chrono::duration_cast<Session::Timestamp::duration>(std::chrono::nanoseconds{value})};
}
}
SqliteStore::SqliteStore(const QString& path)
    : connection_("dimepenny-" + QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    try {
        if (path.isEmpty()) throw StorageError("Database path must not be empty");
        if (path != ":memory:") {
            lock_ = std::make_unique<QLockFile>(QFileInfo(path).absoluteFilePath() + ".lock");
            if (!lock_->tryLock(0)) throw StorageError("Saved data is in use or its directory is not writable");
        }
        db_ = QSqlDatabase::addDatabase("QSQLITE", connection_);
        db_.setDatabaseName(path);
        // Bound contention on the GUI thread; fail visibly rather than stall for seconds.
        db_.setConnectOptions("QSQLITE_BUSY_TIMEOUT=100");
        if (!db_.open()) throw StorageError(db_.lastError().text().toStdString());
        query(db_, "PRAGMA foreign_keys=ON");
        migrate();
        query(db_, "PRAGMA journal_mode=WAL");
        query(db_, "PRAGMA synchronous=FULL");
    } catch (...) {
        db_.close(); db_ = {}; QSqlDatabase::removeDatabase(connection_); throw;
    }
}
SqliteStore::~SqliteStore() {
    db_.close(); db_ = {}; QSqlDatabase::removeDatabase(connection_);
}
void SqliteStore::migrate()
{
    auto versionQuery = query(db_, "PRAGMA user_version");
    if (!versionQuery.next()) throw StorageError("Cannot read schema version");
    const int version = versionQuery.value(0).toInt();
    versionQuery.finish();
    if (version < 0 || version > schemaVersion) throw StorageError("Database schema is newer than this application");
    Transaction transaction(db_);
    if (version < 1) {
        query(db_, "CREATE TABLE projects (id INTEGER PRIMARY KEY, name TEXT NOT NULL, directory TEXT NOT NULL, UNIQUE(name,directory))");
        query(db_, "CREATE TABLE pet_identities (id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL, appearance TEXT NOT NULL, red INTEGER NOT NULL CHECK(red BETWEEN 0 AND 255), green INTEGER NOT NULL CHECK(green BETWEEN 0 AND 255), blue INTEGER NOT NULL CHECK(blue BETWEEN 0 AND 255), UNIQUE(appearance,red,green,blue))");
        query(db_, "CREATE TABLE sessions (id TEXT PRIMARY KEY NOT NULL, project_id INTEGER NOT NULL REFERENCES projects(id), pet_id TEXT NOT NULL UNIQUE REFERENCES pet_identities(id), terminal TEXT NOT NULL, created_ns INTEGER NOT NULL, codex_id TEXT UNIQUE, process_id INTEGER CHECK(process_id BETWEEN 1 AND 4294967295), state TEXT NOT NULL, enabled INTEGER NOT NULL CHECK(enabled IN (0,1)), last_event_ns INTEGER, last_sequence TEXT)");
        query(db_, "CREATE TABLE registrations (id INTEGER PRIMARY KEY, session_id TEXT NOT NULL REFERENCES sessions(id), message_id TEXT NOT NULL, registered_at TEXT NOT NULL, UNIQUE(session_id,message_id))");
        query(db_, "CREATE TABLE events (session_id TEXT NOT NULL REFERENCES sessions(id), event_id TEXT NOT NULL, type TEXT NOT NULL, occurred_at TEXT NOT NULL, sequence TEXT NOT NULL, payload TEXT NOT NULL, PRIMARY KEY(session_id,event_id), UNIQUE(session_id,sequence))");
        query(db_, "CREATE TABLE settings (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL)");
        query(db_, "PRAGMA user_version=1");
    }
    if (version < 2) {
        query(db_, "CREATE TABLE approval_history (session_id TEXT NOT NULL, event_id TEXT NOT NULL, request_id TEXT NOT NULL, kind TEXT NOT NULL CHECK(kind IN ('ApprovalRequested','ApprovalResolved')), decision TEXT CHECK(decision IN ('approve','deny')), PRIMARY KEY(session_id,event_id), FOREIGN KEY(session_id,event_id) REFERENCES events(session_id,event_id))");
        query(db_, "INSERT INTO approval_history SELECT session_id,event_id,json_extract(payload,'$.approval.requestId'),type,json_extract(payload,'$.approval.decision') FROM events WHERE type IN ('ApprovalRequested','ApprovalResolved')");
        query(db_, "CREATE INDEX approval_request ON approval_history(session_id,request_id)");
        query(db_, "PRAGMA user_version=2");
    }
    transaction.commit();
}
void SqliteStore::save(const Session& s, bool enabled, const ipc::Message* message)
{
    if (message && (message->sessionId != text(s.id().value())
        || (message->type != "event" && message->type != "register")))
        throw StorageError("History does not match session");
    Transaction transaction(db_);
    const auto directory = QString::fromStdWString(s.project().directory().wstring());
    query(db_, "INSERT INTO projects(name,directory) VALUES (?,?) ON CONFLICT(name,directory) DO NOTHING", {text(s.project().name()), directory});
    auto project = query(db_, "SELECT id FROM projects WHERE name=? AND directory=?", {text(s.project().name()), directory});
    if (!project.next()) throw StorageError("Missing project");
    const auto projectId = project.value(0); project.finish();
    const auto color = s.pet().color();
    query(db_, "INSERT INTO pet_identities VALUES (?,?,?,?,?,?) ON CONFLICT(id) DO NOTHING",
        {text(s.pet().id()), text(s.pet().name()), text(s.pet().appearanceKey()), color.red, color.green, color.blue});
    const auto state = toString(s.state());
    query(db_, "INSERT INTO sessions VALUES (?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET state=excluded.state, enabled=excluded.enabled, last_event_ns=excluded.last_event_ns, last_sequence=excluded.last_sequence",
        {text(s.id().value()), projectId, text(s.pet().id()), text(s.terminalType()), ticks(s.createdAt()),
         s.codexSessionId() ? QVariant(text(*s.codexSessionId())) : QVariant{},
         s.processId() ? QVariant::fromValue<qlonglong>(*s.processId()) : QVariant{},
         QString::fromLatin1(state.data(), static_cast<qsizetype>(state.size())), enabled ? 1 : 0,
         s.lastEventAt_ ? QVariant(ticks(*s.lastEventAt_)) : QVariant{},
         s.lastSequence_ ? QVariant(QString::number(*s.lastSequence_)) : QVariant{}});
    if (message && message->type == "register")
        query(db_, "INSERT INTO registrations(session_id,message_id,registered_at) VALUES (?,?,?)",
            {message->sessionId, message->messageId, message->timestamp});
    if (message && message->type == "event") {
        const auto type = message->payload.value("eventType").toString();
        query(db_, "INSERT INTO events VALUES (?,?,?,?,?,?)", {message->sessionId, message->messageId, type,
            message->timestamp, message->payload.value("sequence").toString(),
            QString::fromUtf8(QJsonDocument(message->payload).toJson(QJsonDocument::Compact))});
        if (type == "ApprovalRequested" || type == "ApprovalResolved") {
            const auto approval = message->payload.value("approval").toObject();
            query(db_, "INSERT INTO approval_history VALUES (?,?,?,?,?)", {message->sessionId, message->messageId,
                approval.value("requestId").toString(), type,
                approval.contains("decision") ? QVariant(approval.value("decision").toString()) : QVariant{}});
        }
    }
    transaction.commit();
}
std::vector<SqliteStore::SavedSession> SqliteStore::loadSessions() const
{
    auto integrity = query(db_, "PRAGMA quick_check");
    if (!integrity.next() || integrity.value(0).toString() != "ok") throw StorageError("Database integrity check failed");
    integrity.finish();
    auto references = query(db_, "PRAGMA foreign_key_check");
    if (references.next()) throw StorageError("Saved data contains broken references");
    references.finish();
    std::vector<SavedSession> result;
    auto q = query(db_, "SELECT s.id,p.name,p.directory,i.id,i.name,i.appearance,i.red,i.green,i.blue,s.terminal,s.created_ns,s.codex_id,s.process_id,s.enabled,s.last_event_ns,s.last_sequence FROM sessions s JOIN projects p ON p.id=s.project_id JOIN pet_identities i ON i.id=s.pet_id ORDER BY s.enabled,s.id");
    while (q.next()) {
        std::optional<std::string> codex;
        std::optional<std::uint32_t> process;
        if (!q.value(11).isNull()) codex = q.value(11).toString().toStdString();
        if (!q.value(12).isNull()) process = q.value(12).toUInt();
        Session s{SessionId{q.value(0).toString().toStdString()},
            ProjectInfo{q.value(1).toString().toStdString(), std::filesystem::path{q.value(2).toString().toStdWString()}},
            PetIdentity{q.value(3).toString().toStdString(), q.value(4).toString().toStdString(), q.value(5).toString().toStdString(),
                {static_cast<std::uint8_t>(q.value(6).toInt()), static_cast<std::uint8_t>(q.value(7).toInt()), static_cast<std::uint8_t>(q.value(8).toInt())}},
            q.value(9).toString().toStdString(), timestamp(q.value(10)), codex, process};
        s.state_ = AgentState::Disconnected;
        if (!q.value(14).isNull()) s.lastEventAt_ = timestamp(q.value(14));
        if (!q.value(15).isNull()) {
            bool ok = false; s.lastSequence_ = q.value(15).toString().toULongLong(&ok);
            if (!ok) throw StorageError("Invalid stored sequence");
        }
        auto events = query(db_, "SELECT event_id FROM events WHERE session_id=?", {q.value(0)});
        while (events.next()) s.acceptedEventIds_.insert(events.value(0).toString().toStdString());
        result.push_back({std::move(s), q.value(13).toBool()});
    }
    return result;
}
void SqliteStore::setSetting(const QString& key, const QString& value) {
    if (key.isEmpty()) throw StorageError("Setting key must not be empty");
    query(db_, "INSERT INTO settings VALUES (?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", {key, value});
}
void SqliteStore::markRestoredDisconnected()
{
    Transaction transaction(db_);
    query(db_, "UPDATE sessions SET state='Disconnected' WHERE state <> 'Disconnected'");
    transaction.commit();
}
QString SqliteStore::setting(const QString& key, const QString& fallback) const {
    auto q = query(db_, "SELECT value FROM settings WHERE key=?", {key});
    return q.next() ? q.value(0).toString() : fallback;
}
QJsonObject SqliteStore::registrationPayload(const Session& s) {
    const auto color = s.pet().color();
    QJsonObject result{{"project", QJsonObject{{"name", text(s.project().name())}, {"directory", QString::fromStdWString(s.project().directory().wstring())}}},
        {"pet", QJsonObject{{"id", text(s.pet().id())}, {"name", text(s.pet().name())}, {"appearanceKey", text(s.pet().appearanceKey())},
            {"color", QJsonObject{{"red", color.red}, {"green", color.green}, {"blue", color.blue}}}}}, {"terminalType", text(s.terminalType())}};
    if (s.codexSessionId()) result.insert("codexSessionId", text(*s.codexSessionId()));
    if (s.processId()) result.insert("processId", static_cast<qint64>(*s.processId()));
    return result;
}
}
