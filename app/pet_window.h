#pragma once
#include "dimepenny/domain/session.h"
#include <QQuickView>
namespace dimepenny {
class PetWindow final : public QQuickView {
    Q_OBJECT
    Q_PROPERTY(QString agentName READ agentName NOTIFY changed)
    Q_PROPERTY(QString appearance READ appearance NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString details READ details NOTIFY changed)
    Q_PROPERTY(bool onTop READ onTop NOTIFY changed)
public:
    explicit PetWindow(const Session& session, bool simulated = true, bool interactive = false);
    ~PetWindow() override;
    void updateSession(const Session& session);
    QString sessionId() const { return id_; }
    QString agentName() const { return name_; }
    QString appearance() const { return appearance_; }
    QString state() const { return state_; }
    QString details() const { return details_; }
    bool onTop() const { return flags().testFlag(Qt::WindowStaysOnTopHint); }
    void placeAt(QPoint position);
    Q_INVOKABLE void resetPosition(int slot = 0);
    Q_INVOKABLE void showPet();
    Q_INVOKABLE void showDetails();
    Q_INVOKABLE void hidePet();
    Q_INVOKABLE void setOnTop(bool enabled);
    Q_INVOKABLE void beginDrag(qreal x, qreal y);
    Q_INVOKABLE void dragTo(qreal x, qreal y);
    Q_INVOKABLE void nudge(int dx, int dy);
    Q_INVOKABLE bool openProject();
    Q_INVOKABLE void reconnect();
    static QPoint constrainedPosition(QPoint desired, QSize size, QRect available);
signals:
    void changed();
    void detailsRequested();
    void reconnectRequested(const QString& sessionId);
    void activationRequested();
private:
    QString id_, name_, appearance_, state_, directory_, details_;
    bool simulated_;
    bool interactive_;
    QPoint dragOffset_;
    void watchScreen(QScreen* screen);
};
}
