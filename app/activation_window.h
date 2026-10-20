#pragma once
#include "dimepenny/application/event_processor.h"
#include "dimepenny/ipc/simulated_bridge.h"
#include <QQuickView>
#include <QVariantList>
#include <QMap>
#include <QTimer>
namespace dimepenny {
class PetWindow;
class ActivationWindow final : public QQuickView {
    Q_OBJECT
    Q_PROPERTY(QString validation READ validation NOTIFY changed)
    Q_PROPERTY(bool canActivate READ canActivate NOTIFY changed)
    Q_PROPERTY(QString connection READ connection NOTIFY changed)
    Q_PROPERTY(int capacityUsed READ capacityUsed NOTIFY changed)
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY changed)
    Q_PROPERTY(QString nextName READ nextName NOTIFY changed)
    Q_PROPERTY(int pennyCount READ pennyCount NOTIFY changed)
    Q_PROPERTY(bool dimeReserved READ dimeReserved NOTIFY changed)
public:
    explicit ActivationWindow(const QString& databasePath = {});
    ~ActivationWindow() override;
    QString validation() const;
    bool canActivate() const { return validationError().isEmpty(); }
    QString connection() const { return connection_; }
    int capacityUsed() const { return static_cast<int>(processor_.registry().activeCount()); }
    QVariantList sessions() const { return sessions_; }
    QString nextName() const;
    int pennyCount() const { return static_cast<int>(appearances_.size()) - (dimeReserved() ? 1 : 0); }
    bool dimeReserved() const { return appearances_.values().contains(0); }
    Q_INVOKABLE void configure(const QString& directory, const QString& terminal, int companionType);
    Q_INVOKABLE QString localPath(const QUrl& url) const;
    Q_INVOKABLE void activate();
    Q_INVOKABLE void reconnectSession(const QString& id);
    Q_INVOKABLE void showPet(const QString& id);
    Q_INVOKABLE void showDetails(const QString& id);
signals:
    void changed();
    void selectSession(const QString& id);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    EventProcessor processor_;
    QString endpoint_, directory_, terminal_ = "PowerShell", connection_, pending_;
    int companionType_ = 0;
    bool listening_ = false, closing_ = false;
    QVariantList sessions_;
    QMap<QString, ipc::SimulatedBridge*> bridges_;
    QMap<QString, int> appearances_;
    QMap<QString, QString> names_;
    QMap<QString, PetWindow*> pets_;
    QSet<QString> failed_;
    QSet<QString> dirtyPets_;
    QTimer refreshTimer_;
    QString validationError() const;
    int nextAppearance() const;
    QJsonObject payload(const QString& id) const;
    void refresh();
    void flushRefresh();
    void wireBridge(const QString& id, ipc::SimulatedBridge* bridge);
    void saveWindowSettings();
};
}
