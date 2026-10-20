#include "dimepenny/application/event_processor.h"
#include <QDateTime>
#include <QUuid>

namespace dimepenny {
namespace {
Session::Timestamp timestamp(const QString& text)
{
    const auto milliseconds = QDateTime::fromString(text, Qt::ISODateWithMs).toMSecsSinceEpoch();
    using Duration = Session::Timestamp::duration;
    const auto lower = std::chrono::duration_cast<std::chrono::milliseconds>(Duration::min()).count();
    const auto upper = std::chrono::duration_cast<std::chrono::milliseconds>(Duration::max()).count();
    if (milliseconds < lower || milliseconds > upper) throw std::invalid_argument("Timestamp outside domain range");
    return Session::Timestamp{std::chrono::duration_cast<Duration>(std::chrono::milliseconds{milliseconds})};
}
AgentEventType eventType(const QString& name)
{
    if (name == "SessionConnected") return AgentEventType::SessionConnected;
    if (name == "TaskStarted") return AgentEventType::TaskStarted;
    if (name == "TaskCompleted") return AgentEventType::TaskCompleted;
    if (name == "ApprovalRequested") return AgentEventType::ApprovalRequested;
    if (name == "ApprovalResolved") return AgentEventType::ApprovalResolved;
    if (name == "SessionDisconnected") return AgentEventType::SessionDisconnected;
    if (name == "SessionError") return AgentEventType::SessionError;
    throw std::invalid_argument("Unknown event");
}
ProcessingResult map(const RegistryEventResult& result)
{
    const auto* transition = std::get_if<TransitionResult>(&result);
    if (!transition) return ProcessingResult::UnavailableSession;
    switch (*transition) {
    case TransitionResult::Applied: return ProcessingResult::Applied;
    case TransitionResult::DuplicateEvent: return ProcessingResult::Duplicate;
    case TransitionResult::WrongSession: return ProcessingResult::WrongSession;
    case TransitionResult::OutOfOrder: return ProcessingResult::OutOfOrder;
    case TransitionResult::MissingSequence: return ProcessingResult::InvalidMessage;
    case TransitionResult::InvalidTransition: return ProcessingResult::InvalidTransition;
    case TransitionResult::ApprovalMismatch: return ProcessingResult::ApprovalMismatch;
    }
    return ProcessingResult::InvalidMessage;
}
}

EventProcessor::EventProcessor(ipc::ServerOptions options, QObject* parent)
    : QObject(parent), server_(options)
{
    qRegisterMetaType<ProcessingResult>();
    server_.setRegistrationHandler([this](const ipc::Message& message) { return admit(message); });
    connect(&server_, &ipc::LocalServer::sessionAuthenticated, this, [this](const QString& id) {
        connected_.insert(id);
        emit stateChanged(id);
    });
    connect(&server_, &ipc::LocalServer::authenticatedMessage, this,
        [this](const QString& id, const ipc::Message& message) {
            const auto result = process(id, message);
            if (result != ProcessingResult::Applied && result != ProcessingResult::Duplicate
                && result != ProcessingResult::Ignored) {
                (void)server_.send({ipc::protocolVersion, QUuid::createUuid().toString(QUuid::WithoutBraces), id,
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), "error",
                    {{"code", "event_rejected"}, {"message", "Lifecycle event was rejected"}, {"inReplyTo", message.messageId}}});
            }
        });
    connect(&server_, &ipc::LocalServer::sessionDisconnected, this,
        [this](const QString& id, const QString&) {
            connected_.remove(id);
            registry_.markDisconnected(SessionId{id.toStdString()});
            if (storage_) {
                if (const auto* session = registry_.find(SessionId{id.toStdString()})) {
                    try { storage_->save(*session, registry_.isActive(session->id())); }
                    catch (const StorageError& error) { emit persistenceFailed(QString::fromUtf8(error.what())); }
                }
            }
            emit stateChanged(id);
        });
}
EventProcessor::~EventProcessor() { server_.stop(); }

void EventProcessor::enablePersistence(const QString& path)
{
    if (storage_ || registry_.size() || server_.isListening() || server_.connectionCount())
        throw StorageError("Persistence must be enabled before registration");
    auto store = std::make_unique<SqliteStore>(path);
    SessionRegistry restored;
    for (auto& entry : store->loadSessions())
        if (restored.restoreSession(std::move(entry.session), entry.enabled) != RegistryResult::Registered)
            throw StorageError("Saved sessions violate identity or capacity constraints");
    // Complete recovery durably before publishing any session. A failed reset
    // leaves this processor empty and does not partially expose saved live states.
    store->markRestoredDisconnected();
    registry_ = std::move(restored);
    storage_ = std::move(store);
}

bool EventProcessor::admit(const ipc::Message& message)
{
    try {
        (void)ipc::encodeJson(message); // Validates schema and encoded size before domain conversion.
        if (message.type != "register") return false;
        const auto& payload = message.payload;
        const auto project = payload.value("project").toObject();
        const auto pet = payload.value("pet").toObject();
        const auto color = pet.value("color").toObject();
        std::optional<std::string> codex;
        std::optional<std::uint32_t> pid;
        if (payload.contains("codexSessionId")) codex = payload.value("codexSessionId").toString().toStdString();
        if (payload.contains("processId")) pid = static_cast<std::uint32_t>(payload.value("processId").toDouble());
        Session session{SessionId{message.sessionId.toStdString()},
            ProjectInfo{project.value("name").toString().toStdString(),
                        std::filesystem::path{project.value("directory").toString().toStdWString()}},
            PetIdentity{pet.value("id").toString().toStdString(), pet.value("name").toString().toStdString(),
                        pet.value("appearanceKey").toString().toStdString(),
                        {static_cast<std::uint8_t>(color.value("red").toInt()),
                         static_cast<std::uint8_t>(color.value("green").toInt()),
                         static_cast<std::uint8_t>(color.value("blue").toInt())}},
            payload.value("terminalType").toString().toStdString(), timestamp(message.timestamp), codex, pid};
        auto candidate = registry_;
        if (const auto* existing = registry_.find(session.id())) {
            // Reconnect cannot overwrite identity, project, appearance or terminal metadata.
            if (existing->project() != session.project() || existing->pet() != session.pet()
                || existing->codexSessionId() != session.codexSessionId()
                || existing->terminalType() != session.terminalType()
                || existing->processId() != session.processId()) return false;
            if (candidate.reconnect(session.id()) != RegistryResult::Reconnecting) return false;
        } else if (candidate.registerSession(std::move(session)) != RegistryResult::Registered) return false;
        if (storage_) storage_->save(*candidate.find(SessionId{message.sessionId.toStdString()}), true, &message);
        registry_ = std::move(candidate);
        return true;
    } catch (const ipc::ProtocolError&) { return false; }
      catch (const std::invalid_argument&) { return false; }
      catch (const StorageError& error) { emit persistenceFailed(QString::fromUtf8(error.what())); return false; }
}

ProcessingResult EventProcessor::process(const QString& boundId, const ipc::Message& message)
{
    ProcessingResult result = ProcessingResult::InvalidMessage;
    try {
        (void)ipc::encodeJson(message);
        if (boundId != message.sessionId) result = ProcessingResult::WrongSession;
        else if (!connected_.contains(boundId)) result = ProcessingResult::NotAuthenticated;
        else if (message.type == "error") result = ProcessingResult::Ignored;
        else if (message.type == "event") {
            std::optional<ApprovalInfo> approval;
            if (message.payload.contains("approval")) {
                const auto a = message.payload.value("approval").toObject();
                std::optional<ApprovalDecision> decision;
                if (a.contains("decision")) decision = a.value("decision") == "approve"
                    ? ApprovalDecision::Approve : ApprovalDecision::Deny;
                approval = ApprovalInfo{a.value("requestId").toString().toStdString(), decision};
            }
            const AgentEvent event{message.messageId.toStdString(), SessionId{message.sessionId.toStdString()},
                eventType(message.payload.value("eventType").toString()), timestamp(message.timestamp),
                message.payload.value("sequence").toString().toULongLong(), approval};
            // Publish only after the durable snapshot and event/approval rows commit.
            auto candidate = registry_;
            result = map(candidate.applyEvent(event));
            if (result == ProcessingResult::Applied) {
                if (storage_) storage_->save(*candidate.find(event.sessionId()), true, &message);
                registry_ = std::move(candidate);
                emit stateChanged(boundId);
            }
        }
    } catch (const ipc::ProtocolError&) { result = ProcessingResult::InvalidMessage; }
      catch (const std::invalid_argument&) { result = ProcessingResult::InvalidMessage; }
      catch (const StorageError& error) {
          result = ProcessingResult::PersistenceFailed;
          emit persistenceFailed(QString::fromUtf8(error.what()));
      }
    emit eventProcessed(boundId, message.messageId, result);
    return result;
}
}
