#include "activation_window.h"
#include "pet_window.h"
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QQmlContext>
#include <QGuiApplication>
#include <QStyleHints>
#include <QQuickStyle>
#include <QUuid>
static void initializeUiResources() { Q_INIT_RESOURCE(ui); }
namespace dimepenny {
namespace {
QString uniqueId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
const QStringList appearances = {"Cat", "Dog", "Fox", "Rabbit", "Owl", "Panda", "Bear", "Otter", "Penguin", "Turtle"};
const QStringList terminals = {"PowerShell", "Command Prompt", "Git Bash", "WSL"};
}
ActivationWindow::ActivationWindow()
{
    initializeUiResources();
    QQuickStyle::setStyle("FluentWinUI3");
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    setTitle("Dime&Penny");
    setResizeMode(QQuickView::SizeRootObjectToView);
    setMinimumSize({720, 640}); resize(1000, 760);
    rootContext()->setContextProperty("backend", this);
    connect(&processor_, &EventProcessor::stateChanged, this, [this](const QString& id) {
        const auto* session = processor_.registry().find(SessionId{id.toStdString()});
        if (session && id == pending_ && session->state() == AgentState::Active) {
            pending_.clear(); connection_ = "Connected: " + names_.value(id);
        }
        refresh();
    });
    endpoint_ = "dimepenny-ui-" + uniqueId();
    listening_ = processor_.server().listen(endpoint_);
    connection_ = listening_ ? "Ready for your first session" : "Local server unavailable: " + processor_.server().errorString();
    setSource(QUrl("qrc:/qml/Activation.qml"));
}
ActivationWindow::~ActivationWindow()
{
    closing_ = true;
    for (auto* pet : pets_) delete pet;
    for (auto* bridge : bridges_) { bridge->disconnect(this); delete bridge; }
    processor_.disconnect(this); processor_.server().stop();
    setSource(QUrl());
}
void ActivationWindow::closeEvent(QCloseEvent* event)
{
    closing_ = true;
    for (auto* pet : pets_) pet->hide();
    processor_.server().stop();
    QQuickView::closeEvent(event);
}
void ActivationWindow::configure(const QString& name, const QString& directory, const QString& terminal, int appearance)
{
    name_ = name; directory_ = directory; terminal_ = terminal; appearance_ = appearance; emit changed();
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
    if (name_.trimmed().isEmpty()) return "Enter an agent name.";
    const QFileInfo path(directory_.trimmed());
    if (directory_.trimmed().isEmpty() || !path.isAbsolute() || !path.isDir() || !path.isReadable())
        return "Choose an existing, readable project folder.";
    if (appearance_ < 0 || appearance_ >= appearances.size() || !terminals.contains(terminal_))
        return "Choose a terminal and pet appearance.";
    if (appearances_.values().contains(appearance_)) return "That pet is reserved. Choose another or reconnect its session.";
    try {
        (void)ipc::encodeJson({ipc::protocolVersion, "validation", "validation",
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs), "register", payload("validation")});
    } catch (const std::exception&) { return "The name or folder exceeds the allowed text limits."; }
    return {};
}
QJsonObject ActivationWindow::payload(const QString& id) const
{
    const QFileInfo path(directory_.trimmed());
    return {{"project", QJsonObject{{"name", path.fileName().isEmpty() ? QString("Project") : path.fileName()}, {"directory", path.canonicalFilePath()}}},
        {"pet", QJsonObject{{"id", id}, {"name", name_.trimmed()}, {"appearanceKey", appearances.value(appearance_).toLower()},
            {"color", QJsonObject{{"red", 64}, {"green", 128}, {"blue", 160}}}}},
        {"terminalType", terminal_}};
}
void ActivationWindow::activate()
{
    if (!canActivate()) { emit changed(); return; }
    const auto id = uniqueId();
    auto* bridge = new ipc::SimulatedBridge(endpoint_, id, processor_.server().authorize(id), payload(id), this);
    bridges_.insert(id, bridge); appearances_.insert(id, appearance_);
    names_.insert(id, name_.trimmed()); pending_ = id;
    connect(bridge, &ipc::SimulatedBridge::registered, this, [bridge] { (void)bridge->sendEvent("SessionConnected"); });
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
    connection_ = "Connecting: " + names_.value(id); refresh(); bridge->connectToServer();
}
void ActivationWindow::refresh()
{
    if (closing_) return;
    sessions_.clear();
    for (const auto* session : processor_.registry().activeSessions()) {
        const auto id = QString::fromStdString(session->id().value());
        auto* pet = pets_.value(id);
        if (!pet) {
            pet = new PetWindow(*session);
            pets_.insert(id, pet); pet->resetPosition(static_cast<int>(pets_.size()) - 1);
            connect(pet, &PetWindow::reconnectRequested, this, &ActivationWindow::reconnectSession);
            connect(pet, &PetWindow::activationRequested, this, [this, id] { showNormal(); raise(); requestActivate(); emit selectSession(id); });
            if (pet->status() == QQuickView::Ready) pet->showPet();
            else connection_ = "Could not load a pet window. See application diagnostics.";
        } else pet->updateSession(*session);
        const auto state = toString(session->state());
        sessions_.append(QVariantMap{{"sessionId", id}, {"name", QString::fromStdString(session->pet().name())},
            {"appearance", appearances.value(appearances_.value(id))}, {"terminal", QString::fromStdString(session->terminalType())},
            {"state", QString::fromLatin1(state.data(), static_cast<qsizetype>(state.size()))},
            {"canReconnect", pending_.isEmpty() && session->state() == AgentState::Disconnected}});
    }
    emit changed();
}
void ActivationWindow::reconnectSession(const QString& id)
{
    if (!bridges_.contains(id) || !pending_.isEmpty() || !listening_ || closing_) return;
    const auto* session = processor_.registry().find(SessionId{id.toStdString()});
    if (!session || session->state() != AgentState::Disconnected) return;
    failed_.remove(id); pending_ = id; connection_ = "Reconnecting: " + names_.value(id);
    refresh(); bridges_.value(id)->connectToServer();
}
void ActivationWindow::showPet(const QString& id) { if (auto* pet = pets_.value(id)) pet->showPet(); }
void ActivationWindow::showDetails(const QString& id) { if (auto* pet = pets_.value(id)) { pet->showPet(); pet->showDetails(); } }
}
