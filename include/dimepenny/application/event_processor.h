#pragma once
#include "dimepenny/domain/session_registry.h"
#include "dimepenny/ipc/local_server.h"
#include <QSet>
#include "dimepenny/persistence/sqlite_store.h"
#include <memory>

namespace dimepenny {
enum class ProcessingResult {
    Applied, Duplicate, InvalidMessage, WrongSession, NotAuthenticated,
    InvalidTransition, OutOfOrder, ApprovalMismatch, UnavailableSession, Ignored, PersistenceFailed
};

// Owns transport and registry so callbacks cannot outlive domain state.
class EventProcessor final : public QObject {
    Q_OBJECT
public:
    explicit EventProcessor(ipc::ServerOptions options = {}, QObject* parent = nullptr);
    ~EventProcessor() override;
    [[nodiscard]] ipc::LocalServer& server() noexcept { return server_; }
    [[nodiscard]] const SessionRegistry& registry() const noexcept { return registry_; }
    // Call before listening/registration; failures leave the existing registry untouched.
    void enablePersistence(const QString& path);
    SqliteStore* storage() const { return storage_.get(); }
    // Trusted adapter entry: boundSessionId must come from authenticated transport.
    [[nodiscard]] ProcessingResult process(const QString& boundSessionId, const ipc::Message& message);
signals:
    void stateChanged(const QString& sessionId);
    void persistenceFailed(const QString& reason);
    void eventProcessed(const QString& sessionId, const QString& eventId, dimepenny::ProcessingResult result);
private:
    SessionRegistry registry_;
    std::unique_ptr<SqliteStore> storage_;
    QSet<QString> connected_;
    ipc::LocalServer server_;
    bool admit(const ipc::Message& message);
};
}
Q_DECLARE_METATYPE(dimepenny::ProcessingResult)
