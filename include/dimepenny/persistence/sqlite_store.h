#pragma once
#include "dimepenny/domain/session.h"
#include "dimepenny/ipc/protocol.h"
#include <QSqlDatabase>
#include <stdexcept>
#include <vector>
#include <QLockFile>
#include <memory>

namespace dimepenny {
class StorageError : public std::runtime_error { using std::runtime_error::runtime_error; };
// Single-threaded repository. Each write commits a snapshot and its history together.
class SqliteStore final {
public:
    static constexpr int schemaVersion = 2;
    explicit SqliteStore(const QString& path);
    ~SqliteStore();
    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;
    struct SavedSession { Session session; bool enabled; };
    std::vector<SavedSession> loadSessions() const;
    // Call after validating all restored records, before exposing them or listening.
    // History and reservations are untouched; connection liveness never survives restart.
    void markRestoredDisconnected();
    void save(const Session& session, bool enabled, const ipc::Message* message = nullptr);
    void setSetting(const QString& key, const QString& value);
    QString setting(const QString& key, const QString& fallback = {}) const;
    static QJsonObject registrationPayload(const Session& session);
private:
    QString connection_;
    QSqlDatabase db_;
    std::unique_ptr<QLockFile> lock_;
    void migrate();
};
}
