#include "pet_window.h"
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QQmlContext>
#include <QScreen>
#include <QTimer>
#include <QStyleHints>
#include <QQuickStyle>
#include <algorithm>
#include <limits>
static void initializePetResources() { Q_INIT_RESOURCE(ui); }
namespace dimepenny {
PetWindow::PetWindow(const Session& session, bool simulated, bool interactive)
    : id_(QString::fromStdString(session.id().value())), simulated_(simulated), interactive_(interactive)
{
    initializePetResources();
    if (QQuickStyle::name().isEmpty()) QQuickStyle::setStyle("FluentWinUI3");
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    setObjectName("desktopPet");
    setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
    resize(200, 190); setMinimumSize(size()); setMaximumSize(size());
    rootContext()->setContextProperty("pet", this);
    updateSession(session);
    setSource(QUrl("qrc:/qml/Pet.qml"));
    for (auto* screen : QGuiApplication::screens()) watchScreen(screen);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &PetWindow::watchScreen);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] {
        QTimer::singleShot(0, this, [this] { placeAt(position()); });
    });
}
PetWindow::~PetWindow() { setSource(QUrl()); }
void PetWindow::watchScreen(QScreen* screen)
{
    connect(screen, &QScreen::availableGeometryChanged, this, [this] { placeAt(position()); });
}
void PetWindow::updateSession(const Session& session)
{
    if (id_ != QString::fromStdString(session.id().value())) return;
    name_ = QString::fromStdString(session.pet().name());
    appearance_ = QString::fromStdString(session.pet().appearanceKey());
    const auto state = toString(session.state());
    state_ = QString::fromLatin1(state.data(), static_cast<qsizetype>(state.size()));
    directory_ = QDir::toNativeSeparators(QString::fromStdWString(session.project().directory().wstring()));
    details_ = QString("Agent: %1\nSession: %2\nProject: %3\nDirectory: %4\nTerminal: %5\nPet: %6\nState: %7\n\nSimulated session\nDrag to move. Arrow keys: 10 px; Shift: 1 px.\nDouble-click / Enter: details. Right-click / Shift+F10: menu.")
        .arg(name_, id_, QString::fromStdString(session.project().name()), directory_,
             QString::fromStdString(session.terminalType()), appearance_, state_);
    if (!simulated_) details_.replace("Simulated session", interactive_
        ? "Interactive Codex (trusted hooks; no terminal text)\nCompleted means Stop hook observed, not verified success."
        : "Codex exec session (structured JSON lifecycle)");
    setTitle(name_ + " — Dime&Penny"); emit changed();
}
QPoint PetWindow::constrainedPosition(QPoint desired, QSize size, QRect area)
{
    return {std::clamp(desired.x(), area.left(), std::max(area.left(), area.right() - size.width() + 1)),
            std::clamp(desired.y(), area.top(), std::max(area.top(), area.bottom() - size.height() + 1))};
}
void PetWindow::placeAt(QPoint desired)
{
    // Find the nearest work area, including monitors with negative coordinates.
    QScreen* nearest = nullptr; qint64 distance = std::numeric_limits<qint64>::max();
    for (auto* screen : QGuiApplication::screens()) {
        const auto candidate = constrainedPosition(desired, size(), screen->availableGeometry());
        const qint64 dx = qint64(candidate.x()) - desired.x(), dy = qint64(candidate.y()) - desired.y();
        if (dx * dx + dy * dy < distance) { distance = dx * dx + dy * dy; nearest = screen; }
    }
    if (nearest) setPosition(constrainedPosition(desired, size(), nearest->availableGeometry()));
}
void PetWindow::resetPosition(int slot)
{
    if (auto* screen = QGuiApplication::primaryScreen()) {
        const auto area = screen->availableGeometry();
        const int columns = std::max(1, area.width() / width());
        const int index = std::max(0, slot);
        placeAt({area.right() - width() - (index % columns) * width(),
                 area.bottom() - height() - (index / columns) * height()});
    }
}

void PetWindow::showPet() { placeAt(position()); show(); raise(); }
void PetWindow::showDetails() { emit detailsRequested(); }
void PetWindow::hidePet() { hide(); }
void PetWindow::setOnTop(bool enabled)
{
    const auto at = position(); const bool visible = isVisible();
    auto updated = flags(); updated.setFlag(Qt::WindowStaysOnTopHint, enabled);
    setFlags(updated); if (visible) show(); placeAt(at); emit changed();
}
void PetWindow::beginDrag(qreal x, qreal y) { dragOffset_ = QPoint(qRound(x), qRound(y)) - position(); }
void PetWindow::dragTo(qreal x, qreal y) { placeAt(QPoint(qRound(x), qRound(y)) - dragOffset_); }
void PetWindow::nudge(int dx, int dy) { placeAt(position() + QPoint(dx, dy)); }
bool PetWindow::openProject() { return QDesktopServices::openUrl(QUrl::fromLocalFile(directory_)); }
void PetWindow::reconnect() { if (state_ == "Disconnected") emit reconnectRequested(id_); }
}
