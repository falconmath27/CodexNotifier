#include "pet_window.h"
#include "dimepenny/domain/agent_state_machine.h"
#include <QQuickItem>
#include <QScreen>
#include <QtTest>
namespace {
dimepenny::Session session(const char* id = "session-1") {
    return {dimepenny::SessionId{id}, dimepenny::ProjectInfo{"Example", std::filesystem::path{L"C:/Projects/Example"}},
        dimepenny::PetIdentity{id, "Frontend <helper>", "cat", {64,128,160}}, "PowerShell", std::chrono::system_clock::now()};
}
}
class PetWindowTest : public QObject {
    Q_OBJECT
private slots:
    void qmlInteractions()
    {
        auto model = session(); dimepenny::PetWindow pet(model);
        QCOMPARE(pet.status(), QQuickView::Ready);
        QVERIFY(pet.flags().testFlag(Qt::FramelessWindowHint)); QVERIFY(pet.onTop());
        QCOMPARE(pet.color(), QColor(Qt::transparent));
        const auto area = QGuiApplication::primaryScreen()->availableGeometry();
        pet.placeAt(area.topLeft() + QPoint(60,60)); pet.showPet();
        QVERIFY(QTest::qWaitForWindowExposed(&pet));
        const auto start = pet.position();
        QTest::mouseClick(&pet, Qt::LeftButton, Qt::NoModifier, QPoint(100,130));
        QTest::keyClick(&pet, Qt::Key_Right); QCOMPARE(pet.position(), start + QPoint(10,0));
        QTest::keyClick(&pet, Qt::Key_Down, Qt::ShiftModifier); QCOMPARE(pet.position(), start + QPoint(10,1));
        QTest::mousePress(&pet, Qt::LeftButton, Qt::NoModifier, QPoint(100,130));
        QTest::mouseMove(&pet, QPoint(120,140));
        QTest::mouseRelease(&pet, Qt::LeftButton, Qt::NoModifier, QPoint(100,130));
        QVERIFY(pet.position() != start + QPoint(10,1));
        QTest::keyClick(&pet, Qt::Key_Return);
        auto* details = pet.rootObject()->findChild<QWindow*>("petSessionDetails");
        QVERIFY(details); QTRY_VERIFY(details->isVisible());
        QVERIFY(pet.details().contains("Frontend <helper>"));
        (void)dimepenny::AgentStateMachine::beginConnecting(model); pet.updateSession(model);
        QCOMPARE(pet.state(), QString("Connecting"));
        pet.updateSession(session("other")); QCOMPARE(pet.state(), QString("Connecting"));
        details->close();
        QTest::mouseClick(&pet, Qt::LeftButton, Qt::NoModifier, QPoint(100,130));
        QTest::keyClick(&pet, Qt::Key_F10, Qt::ShiftModifier);
        auto* menu = pet.rootObject()->findChild<QObject*>("petMenu"); QVERIFY(menu);
        QTRY_VERIFY(menu->property("visible").toBool());
        QMetaObject::invokeMethod(menu, "close");
        auto* onTop = pet.rootObject()->findChild<QObject*>("petOnTop"); QVERIFY(onTop);
        QMetaObject::invokeMethod(onTop, "triggered"); QVERIFY(!pet.onTop());
        QMetaObject::invokeMethod(onTop, "triggered"); QVERIFY(pet.onTop());
        dimepenny::AgentStateMachine::deactivate(model); pet.updateSession(model);
        QSignalSpy requested(&pet, &dimepenny::PetWindow::reconnectRequested);
        auto* reconnect = pet.rootObject()->findChild<QObject*>("petReconnect"); QVERIFY(reconnect->property("enabled").toBool());
        QMetaObject::invokeMethod(reconnect, "triggered"); QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.first().first().toString(), pet.sessionId());
        pet.showDetails(); QTRY_VERIFY(details->isVisible());
        pet.hidePet(); QVERIFY(!pet.isVisible()); QTRY_VERIFY(!details->isVisible());
        pet.updateSession(model); QVERIFY(!pet.isVisible());
        pet.showPet(); QTest::qWait(100);
        const auto image = pet.grabWindow(); QVERIFY(!image.isNull()); QCOMPARE(image.pixelColor(0,0).alpha(), 0);
        if (qEnvironmentVariableIsSet("DIMEPENNY_CAPTURE_UI")) QVERIFY(image.save("pet-qml.png"));
    }
    void bounds()
    {
        using dimepenny::PetWindow;
        QCOMPARE(PetWindow::constrainedPosition({-2500,2000}, {200,190}, {-1920,0,1920,1080}), QPoint(-1920,890));
        QCOMPARE(PetWindow::constrainedPosition({500,500}, {200,190}, {0,0,100,80}), QPoint(0,0));
    }
};
QTEST_MAIN(PetWindowTest)
#include "pet_window_test.moc"
