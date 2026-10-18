#pragma once
#include "dimepenny/domain/session_registry.h"
#include "dimepenny/ipc/local_server.h"
#include <QSet>

namespace dimepenny {
enum class ProcessingResult {
    Applied, Duplicate, InvalidMessage, WrongSession, NotAuthenticated,
    InvalidTransition, OutOfOrder, ApprovalMismatch, UnavailableSession, Ignored
};

// Owns transport and registry so callbacks cannot outlive domain state.
class EventProcessor final : public QObject {
    Q_OBJECT
public:
    explicit EventProcessor(ipc::ServerOptions options = {}, QObject* parent = nullptr);
    ~EventProcessor() override;
    [[nodiscard]] ipc::LocalServer& server() noexcept { return server_; }
    [[nodiscard]] const SessionRegistry& registry() const noexcept { return registry_; }
    // Trusted adapter entry: boundSessionId must come from authenticated transport.
    [[nodiscard]] ProcessingResult process(const QString& boundSessionId, const ipc::Message& message);
signals:
    void stateChanged(const QString& sessionId);
    void eventProcessed(const QString& sessionId, const QString& eventId, dimepenny::ProcessingResult result);
private:
    SessionRegistry registry_;
    QSet<QString> connected_;
    ipc::LocalServer server_;
    bool admit(const ipc::Message& message);
};
}
Q_DECLARE_METATYPE(dimepenny::ProcessingResult)
