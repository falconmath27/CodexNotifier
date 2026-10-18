#include "activation_window.h"
#include "pet_window.h"
#include <QQuickItem>
#include <QTemporaryDir>
#include <QtTest>
class ActivationWindowTest : public QObject {
    Q_OBJECT
private slots:
    void activationAndReconnect()
    {
        QTemporaryDir project; QVERIFY(project.isValid());
        dimepenny::ActivationWindow window;
        QCOMPARE(window.status(), QQuickView::Ready);
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* name = window.rootObject()->findChild<QObject*>("agentName");
        auto* path = window.rootObject()->findChild<QObject*>("projectDirectory");
        auto* appearance = window.rootObject()->findChild<QObject*>("petAppearance");
        auto* button = window.rootObject()->findChild<QQuickItem*>("activateButton");
        QVERIFY(name && path && appearance && button);
        QVERIFY(!window.canActivate());
        name->setProperty("text", "Helper");
        path->setProperty("text", project.path() + "/missing");
        QVERIFY(!window.canActivate());
        path->setProperty("text", project.path()); QVERIFY(window.canActivate());
        name->setProperty("text", QString(257, 'a')); QVERIFY(!window.canActivate());
        name->setProperty("text", "Helper");
        for (int i = 0; i < 10; ++i) {
            appearance->setProperty("currentIndex", i); QVERIFY(window.canActivate());
            QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
            QVERIFY(!window.canActivate());
            QTRY_VERIFY_WITH_TIMEOUT(window.connection().startsWith("Connected:"), 5000);
            QCOMPARE(window.capacityUsed(), i + 1);
            QCOMPARE(window.sessions().size(), i + 1);
        }
        QVERIFY(window.validation().contains("10"));
        const auto id = window.sessions().first().toMap().value("sessionId").toString();
        dimepenny::PetWindow* pet = nullptr;
        int count = 0;
        for (auto* candidate : QGuiApplication::topLevelWindows()) {
            if (auto* p = qobject_cast<dimepenny::PetWindow*>(candidate)) {
                ++count; QCOMPARE(p->status(), QQuickView::Ready);
                if (p->sessionId() == id) pet = p;
            }
        }
        QCOMPARE(count, 10); QVERIFY(pet);
        pet->hidePet(); window.showPet(id); QVERIFY(pet->isVisible());
        const auto at = pet->position();
        auto bridges = window.findChildren<dimepenny::ipc::SimulatedBridge*>();
        QCOMPARE(bridges.size(), 10);
        for (auto* bridge : bridges) bridge->disconnectFromServer();
        QTRY_VERIFY(pet->state() == "Disconnected");
        pet->hidePet(); window.reconnectSession(id);
        QTRY_COMPARE(pet->state(), QString("Active"));
        QVERIFY(!pet->isVisible()); QCOMPARE(pet->position(), at);
        QCOMPARE(window.sessions().size(), 10);
        window.showPet(id);
        if (qEnvironmentVariableIsSet("DIMEPENNY_CAPTURE_UI")) {
            QTest::qWait(100);
            QVERIFY(window.grabWindow().save("activation-qml.png"));
        }
        window.close(); QVERIFY(!pet->isVisible());
    }
};
QTEST_MAIN(ActivationWindowTest)
#include "activation_window_test.moc"
