#include "activation_window.h"
#include "pet_window.h"
#include "dimepenny/integration/codex_exec.h"
#include <QCloseEvent>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QQmlContext>
#include <QGuiApplication>
#include <QStyleHints>
#include <QQuickStyle>
#include <QUuid>
#include <utility>
static void initializeUiResources() { Q_INIT_RESOURCE(ui); }
namespace dimepenny {
namespace {
QString uniqueId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
const QStringList appearances = {"Orange", "Yellow", "Pink", "Green", "Blue", "Purple", "White", "Blue-Green", "Grey", "Golden"};
const QList<QColor> appearanceColors = {
    QColor("#ff6f2c"), QColor("#f5c84b"), QColor("#ef7fae"), QColor("#64c28a"), QColor("#5b8def"),
    QColor("#9b6de3"), QColor("#f2f2f2"), QColor("#3db7b0"), QColor("#8b95a5"), QColor("#d4a72c")
};
const QStringList terminals = {"PowerShell", "Command Prompt", "Git Bash", "WSL"};
}
ActivationWindow::ActivationWindow(const QString& databasePath)
{
    initializeUiResources();
    QQuickStyle::setStyle("FluentWinUI3");
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    setTitle("Dime&Penny");
    setResizeMode(QQuickView::SizeRootObjectToView);
    setMinimumSize({720, 640}); resize(1000, 760);
    rootContext()->setContextProperty("backend", this);
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(16);
    connect(&refreshTimer_, &QTimer::timeout, this, &ActivationWindow::flushRefresh);
    connect(&processor_, &EventProcessor::stateChanged, this, [this](const QString& id) {
        dirtyPets_.insert(id);
        const auto* session = processor_.registry().find(SessionId{id.toStdString()});
        if (session && id == pending_ && session->state() == AgentState::Active) {
            pending_.clear(); connection_ = "Connected: " + names_.value(id);
        }
        refresh();
    });
    endpoint_ = "dimepenny-ui-" + uniqueId();
    connect(&processor_, &EventProcessor::persistenceFailed, this, [this](const QString& reason) {
        connection_ = "Storage error: " + reason; emit changed();
    });
    try {
        if (!databasePath.isEmpty()) processor_.enablePersistence(databasePath);
        if (auto* store = processor_.storage()) {
            resize(qBound(720, store->setting("activation.width", "1000").toInt(), 3840),
                   qBound(640, store->setting("activation.height", "760").toInt(), 2160));
        }
        listening_ = processor_.server().listen(endpoint_);
        connection_ = listening_ ? "Ready for your first session" : "Local server unavailable: " + processor_.server().errorString();
        for (const auto* session : processor_.registry().activeSessions()) {
            const auto id = QString::fromStdString(session->id().value());
            const auto appearance = appearances.indexOf(QString::fromStdString(session->pet().appearanceKey()), 0, Qt::CaseInsensitive);
            if (appearance < 0) throw StorageError("Unknown saved pet appearance");
            appearances_.insert(id, static_cast<int>(appearance));
            names_.insert(id, QString::fromStdString(session->pet().name()));
            const auto source = processor_.storage()->setting("source/" + id);
            if (source == "codex-exec" || source == "codex-interactive") {
                realSessions_.insert(id);
                if (source == "codex-interactive") {
                    interactiveSessions_.insert(id);
                    const auto codexId = processor_.storage()->setting("codex-thread/" + id);
                    if (!codexId.isEmpty()) interactiveIdentities_.insert(id, codexId);
                }
                continue; // Never reconnect a real run using a simulated bridge.
            }
            auto* bridge = new ipc::SimulatedBridge(endpoint_, id, processor_.server().authorize(id), SqliteStore::registrationPayload(*session), this);
            if (session->lastSequence()) bridge->resumeAfter(*session->lastSequence());
            bridges_.insert(id, bridge); wireBridge(id, bridge);
        }
        if (processor_.registry().size()) connection_ = "Saved sessions restored. Reconnect when ready.";
    } catch (const std::exception& error) {
        listening_ = false; processor_.server().stop();
        connection_ = "Could not open saved data: " + QString::fromUtf8(error.what());
    }
    setSource(QUrl("qrc:/qml/Activation.qml"));
    if (listening_) refresh();
}
ActivationWindow::~ActivationWindow()
{
    delete interactive_; interactive_ = nullptr;
    closing_ = true;
    refreshTimer_.stop();
    saveWindowSettings();
    for (auto* adapter : findChildren<CodexExec*>()) delete adapter;
    for (auto* pet : pets_) delete pet;
    const auto bridges = bridges_;
    for (auto* bridge : bridges) { bridge->disconnect(this); delete bridge; }
    processor_.disconnect(this); processor_.server().stop();
    setSource(QUrl());
}
void ActivationWindow::closeEvent(QCloseEvent* event)
{
    if (interactive_) interactive_->stop();
    closing_ = true;
    refreshTimer_.stop();
    saveWindowSettings();
    for (auto* adapter : findChildren<CodexExec*>()) delete adapter;
    for (auto* pet : pets_) pet->hide();
    processor_.server().stop();
    QQuickView::closeEvent(event);
}
void ActivationWindow::saveWindowSettings()
{
    if (auto* store = processor_.storage()) {
        try {
            store->setSetting("activation.width", QString::number(width()));
            store->setSetting("activation.height", QString::number(height()));
        } catch (const StorageError& error) {
            qWarning("Could not save window settings: %s", error.what());
        }
    }
}
void ActivationWindow::configure(const QString& directory, const QString& terminal, int companionType)
{
    directory_ = directory; terminal_ = terminal; companionType_ = companionType; emit changed();
}
int ActivationWindow::nextAppearance() const
{
    if (companionType_ == 0) return dimeReserved() ? -1 : 0;
    if (companionType_ != 1) return -1;
    for (int i = 1; i < appearances.size(); ++i)
        if (!appearances_.values().contains(i)) return i;
    return -1;
}
QString ActivationWindow::nextName() const
{
    if (nextAppearance() < 0) return {};
    return companionType_ == 0 ? QString("Dime") : QString("Penny%1").arg(pennyCount() + 1);
}
QString ActivationWindow::localPath(const QUrl& url) const { return url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : QString{}; }
QString ActivationWindow::validation() const
{
    const auto error = validationError(); return error.isEmpty() ? "Ready to activate" : error;
}
QString ActivationWindow::validationError() const
{
    if (!listening_ || closing_) return "The local server is offline. Restart the app to retry.";
    if (!pending_.isEmpty()) return "Connecting… waiting for the session";
    if (capacityUsed() >= 10) return "All 10 session slots are reserved.";
    if (companionType_ < 0 || companionType_ > 1) return "Choose Dime or Pennies.";
    if (nextAppearance() < 0)
        return companionType_ == 0 ? "Dime is already activated. Choose Pennies or reconnect Dime."
                                   : "All 9 Pennies are reserved. Reconnect an existing Penny.";
    const QFileInfo path(directory_.trimmed());
    if (directory_.trimmed().isEmpty() || !path.isAbsolute() || !path.isDir() || !path.isReadable())
        return "Choose an existing, readable project folder.";
    if (!terminals.contains(terminal_)) return "Choose a terminal.";
    try {
        (void)ipc::encodeJson({ipc::protocolVersion, "validation", "validation",
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), "register", payload("validation")});
    } catch (const std::exception&) { return "The name or folder exceeds the allowed text limits."; }
    return {};
}
QJsonObject ActivationWindow::payload(const QString& id) const
{
    const QFileInfo path(directory_.trimmed());
    const auto appearance = nextAppearance();
    const auto color = appearanceColors.value(appearance, QColor("#ff6f2c"));
    return {{"project", QJsonObject{{"name", path.fileName().isEmpty() ? QString("Project") : path.fileName()}, {"directory", path.canonicalFilePath()}}},
        {"pet", QJsonObject{{"id", id}, {"name", nextName()}, {"appearanceKey", appearances.value(appearance).toLower()},
            {"color", QJsonObject{{"red", color.red()}, {"green", color.green()}, {"blue", color.blue()}}}}},
        {"terminalType", terminal_}};
}
void ActivationWindow::activate()
{
    if (!canActivate()) { emit changed(); return; }
    const auto id = uniqueId();
    const auto name = nextName();
    const auto appearance = nextAppearance();
    auto* bridge = new ipc::SimulatedBridge(endpoint_, id, processor_.server().authorize(id), payload(id), this);
    bridges_.insert(id, bridge); appearances_.insert(id, appearance);
    names_.insert(id, name); pending_ = id;
    wireBridge(id, bridge);
    connection_ = "Connecting: " + names_.value(id); refresh(); bridge->connectToServer();
}
void ActivationWindow::wireBridge(const QString& id, ipc::SimulatedBridge* bridge)
{
    connect(bridge, &ipc::SimulatedBridge::registered, this, [this, id, bridge] {
        if (!realSessions_.contains(id)) (void)bridge->sendEvent("SessionConnected");
    });
    connect(bridge, &ipc::SimulatedBridge::failed, this, [this, id](const QString& reason) {
        failed_.insert(id); if (pending_ == id) pending_.clear();
        connection_ = "Connection failed for " + names_.value(id) + ": " + reason;
        if (!processor_.registry().find(SessionId{id.toStdString()})) {
            appearances_.remove(id); names_.remove(id);
            if (auto* failed = bridges_.take(id)) { failed->disconnect(this); failed->deleteLater(); }
            processor_.server().revoke(id);
        }
        refresh();
    });
    connect(bridge, &ipc::SimulatedBridge::disconnected, this, [this, id] {
        if (pending_ == id) pending_.clear();
        if (!failed_.contains(id)) connection_ = "Disconnected: " + names_.value(id) + ". Its pet stays reserved.";
        refresh();
    });
}
void ActivationWindow::activateCodex(const QString& executable, const QString& prompt)
{
    if (!canActivate()) { emit changed(); return; }
    const auto id = uniqueId();
    // Persist source intent before registration so crash recovery cannot simulate it.
    try {
        if (auto* store = processor_.storage()) store->setSetting("source/" + id, "codex-exec");
    } catch (const StorageError& error) {
        connection_ = "Cannot save Codex source: " + QString::fromUtf8(error.what()); emit changed(); return;
    }
    auto* bridge = new ipc::BridgeClient(endpoint_, id, processor_.server().authorize(id), payload(id), this);
    auto* adapter = new CodexExec(this);
    const auto appearance = nextAppearance();
    const auto name = nextName();
    realSessions_.insert(id);
    bridges_.insert(id, bridge); appearances_.insert(id, appearance); names_.insert(id, name);
    pending_ = id;
    wireBridge(id, bridge);
    const auto project = directory_;
    connect(bridge, &ipc::BridgeClient::registered, adapter, [adapter, executable, project, prompt] {
        adapter->start(executable, project, prompt);
    });
    connect(adapter, &CodexExec::lifecycle, this, [this, id, bridge, adapter](const QString& event) {
        if (event == "SessionConnected") {
            try {
                if (auto* store = processor_.storage()) store->setSetting("codex-thread/" + id, adapter->threadId());
            } catch (const StorageError& error) {
                connection_ = "Cannot save Codex thread: " + QString::fromUtf8(error.what()); emit changed();
            }
        }
        if (!bridge->sendEvent(event)) {
            connection_ = "Codex lifecycle delivery failed"; emit changed();
            adapter->deleteLater(); bridge->disconnectFromServer();
        }
    });
    connect(adapter, &CodexExec::diagnostic, this, [this](const QString& reason) { connection_ = reason; emit changed(); });
    connect(adapter, &CodexExec::finished, this, [this, id](bool success) {
        if (pending_ == id) pending_.clear();
        connection_ = success ? "Codex run completed" : "Codex run failed";
        refresh();
    });
    connect(bridge, &ipc::BridgeClient::messageReceived, adapter, [this, adapter, bridge](const ipc::Message& message) {
        if (message.type == "error") {
            connection_ = "Codex lifecycle update rejected; stopping the adapter"; emit changed();
            adapter->deleteLater(); bridge->disconnectFromServer();
        }
    });
    // If the pet connection breaks, do not let a detached adapter continue working.
    connect(bridge, &ipc::BridgeClient::disconnected, adapter, &QObject::deleteLater);
    connection_ = "Connecting Codex: " + names_.value(id); refresh(); bridge->connectToServer();
}
bool ActivationWindow::enableInteractive(const QString& endpoint)
{
    if (!listening_ || closing_ || interactive_) return false;
    auto* server = new InteractiveServer([this](const QString& directory, const QString& id) {
        return registerInteractive(directory, id);
    }, this);
    connect(server, &InteractiveServer::diagnostic, this, [this](const QString& text) {
        connection_ = text; emit changed();
    });
    if (!server->listen(endpoint)) {
        connection_ = "Interactive endpoint unavailable: " + server->errorString();
        delete server; emit changed(); return false;
    }
    interactive_ = server; return true;
}
InteractiveRegistration ActivationWindow::registerInteractive(const QString& directory, const QString& requestedId)
{
    if (!listening_ || closing_) return {};
    const auto id = requestedId.isEmpty() ? uniqueId() : requestedId;
    QJsonObject registration;
    const auto* existing = processor_.registry().find(SessionId{id.toStdString()});
    if (existing) {
        if (!interactiveSessions_.contains(id) || existing->state() != AgentState::Disconnected
            || bridges_.contains(id)
            || QFileInfo(QString::fromStdWString(existing->project().directory().wstring())).canonicalFilePath() != directory) return {};
        registration = SqliteStore::registrationPayload(*existing);
    } else {
        if (appearances_.size() >= 10 || bridges_.contains(id)) return {};
        // External launches do not change the activation form or its pending state.
        const auto previousDirectory = directory_, previousTerminal = terminal_;
        const auto previousType = companionType_;
        directory_ = directory; terminal_ = "PowerShell"; companionType_ = dimeReserved() ? 1 : 0;
        const auto appearance = nextAppearance();
        const auto name = nextName();
        registration = payload(id);
        directory_ = previousDirectory; terminal_ = previousTerminal; companionType_ = previousType;
        if (appearance < 0) return {};
        (void)ipc::encodeJson({ipc::protocolVersion, uniqueId(), id,
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), "register", registration});
        if (auto* store = processor_.storage()) store->setSetting("source/" + id, "codex-interactive");
        appearances_.insert(id, appearance); names_.insert(id, name);
        interactiveSessions_.insert(id); realSessions_.insert(id);
    }
    auto* bridge = new ipc::BridgeClient(endpoint_, id, processor_.server().authorize(id), registration, this);
    if (existing && existing->lastSequence()) bridge->resumeAfter(*existing->lastSequence());
    bridges_.insert(id, bridge);
    wireBridge(id, bridge);
    connect(bridge, &QObject::destroyed, this, [this, id, bridge] {
        if (bridges_.value(id) == bridge) bridges_.remove(id);
        processor_.server().revoke(id);
        if (!processor_.registry().find(SessionId{id.toStdString()})) {
            appearances_.remove(id); names_.remove(id);
            interactiveSessions_.remove(id); realSessions_.remove(id);
        }
    });
    refresh();
    return {id, bridge, [this, id](const QString& codexId) {
        const auto previous = interactiveIdentities_.value(id);
        if (!previous.isEmpty()) return previous == codexId;
        if (interactiveIdentities_.values().contains(codexId)) return false;
        if (auto* store = processor_.storage()) store->setSetting("codex-thread/" + id, codexId);
        interactiveIdentities_.insert(id, codexId);
        return true;
    }};
}
void ActivationWindow::refresh()
{
    // Bound presentation work to one batch per frame. The processor still
    // validates and applies every event; only intermediate visual snapshots coalesce.
    if (!closing_ && !refreshTimer_.isActive()) refreshTimer_.start();
}
void ActivationWindow::flushRefresh()
{
    if (closing_) return;
    const auto dirty = std::exchange(dirtyPets_, {});
    sessions_.clear();
    for (const auto* session : processor_.registry().activeSessions()) {
        const auto id = QString::fromStdString(session->id().value());
        auto* pet = pets_.value(id);
        if (!pet) {
            pet = new PetWindow(*session, !realSessions_.contains(id), interactiveSessions_.contains(id));
            pets_.insert(id, pet); pet->resetPosition(static_cast<int>(pets_.size()) - 1);
            connect(pet, &PetWindow::reconnectRequested, this, &ActivationWindow::reconnectSession);
            connect(pet, &PetWindow::activationRequested, this, [this, id] { showNormal(); raise(); requestActivate(); emit selectSession(id); });
            if (pet->status() == QQuickView::Ready) pet->showPet();
            else connection_ = "Could not load a pet window. See application diagnostics.";
        } else if (dirty.contains(id)) pet->updateSession(*session);
        const auto state = toString(session->state());
        sessions_.append(QVariantMap{{"sessionId", id}, {"name", QString::fromStdString(session->pet().name())},
            {"appearance", appearances_.value(id) == 0 ? "Dime" : "Penny"}, {"terminal", QString::fromStdString(session->terminalType())},
            {"state", QString::fromLatin1(state.data(), static_cast<qsizetype>(state.size()))},
            {"canReconnect", !realSessions_.contains(id) && pending_.isEmpty() && session->state() == AgentState::Disconnected}});
    }
    emit changed();
}
void ActivationWindow::reconnectSession(const QString& id)
{
    if (realSessions_.contains(id)) {
        connection_ = "This is a real Codex run. Start a new explicit run; simulation cannot reconnect it.";
        emit changed(); return;
    }
    if (!bridges_.contains(id) || !pending_.isEmpty() || !listening_ || closing_) return;
    const auto* session = processor_.registry().find(SessionId{id.toStdString()});
    if (!session || session->state() != AgentState::Disconnected) return;
    failed_.remove(id); pending_ = id; connection_ = "Reconnecting: " + names_.value(id);
    refresh(); bridges_.value(id)->connectToServer();
}
void ActivationWindow::showPet(const QString& id) { if (auto* pet = pets_.value(id)) pet->showPet(); }
void ActivationWindow::showDetails(const QString& id) { if (auto* pet = pets_.value(id)) { pet->showPet(); pet->showDetails(); } }
}
