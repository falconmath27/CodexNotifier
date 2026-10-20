#include "pet_window.h"
#include "dimepenny/domain/agent_state_machine.h"
#include <QQuickItem>
#include <QScreen>
#include <QPainter>
#include <QtTest>
#include <array>
namespace {
dimepenny::Session session(const char* id = "session-1", const char* appearance = "orange") {
    return {dimepenny::SessionId{id}, dimepenny::ProjectInfo{"Example", std::filesystem::path{L"C:/Projects/Example"}},
        dimepenny::PetIdentity{id, "Frontend <helper>", appearance, {64,128,160}}, "PowerShell", std::chrono::system_clock::now()};
}
}
class PetWindowTest : public QObject {
    Q_OBJECT
private slots:
    void colorSprites()
    {
        constexpr std::array colors{"orange", "yellow", "pink", "green", "blue", "purple",
                                    "white", "blue-green", "grey", "golden"};
        for (const auto* color : colors) {
            dimepenny::PetWindow pet(session(color, color));
            QCOMPARE(pet.status(), QQuickView::Ready);
            auto* visual = pet.rootObject()->findChild<QObject*>("petVisual");
            QVERIFY(visual);
            QCOMPARE(visual->property("selectedColor").toString(), QString::fromLatin1(color));
            QCOMPARE(visual->property("idleFrameCount").toInt(), QString::fromLatin1(color) == "orange" ? 4 : 2);
        }
        dimepenny::PetWindow fallback(session("legacy", "cat"));
        auto* visual = fallback.rootObject()->findChild<QObject*>("petVisual");
        QVERIFY(visual);
        QCOMPARE(visual->property("selectedColor").toString(), QString("orange"));
    }

    void visualLifecycle()
    {
        using namespace dimepenny;
        auto model = session();
        PetWindow pet(model);
        QCOMPARE(pet.status(), QQuickView::Ready);
        auto* visual = pet.rootObject()->findChild<QObject*>("petVisual");
        auto* motion = pet.rootObject()->findChild<QObject*>("petMotion");
        QVERIFY(visual); QVERIFY(motion);
        QCOMPARE(visual->property("statusLabel").toString(), QString("Not connected"));
        QVERIFY(!visual->property("animationRunning").toBool());
        pet.showPet();
        QVERIFY(QTest::qWaitForWindowExposed(&pet));
        QImage sheet(600, 380, QImage::Format_ARGB32_Premultiplied);
        sheet.fill(QColor("#eef1f6"));
        int captured = 0;
        auto capture = [&] {
            QTest::mouseMove(&pet, QPoint(-30, -30));
            QTest::qWait(80);
            const auto frame = pet.grabWindow();
            QVERIFY(!frame.isNull());
            QPainter painter(&sheet);
            painter.drawImage(QRect((captured % 3) * 200, (captured / 3) * 190, 200, 190), frame);
            ++captured;
        };
        std::uint64_t sequence = 0;
        auto send = [&](AgentEventType type, std::optional<ApprovalInfo> approval = std::nullopt) {
            ++sequence;
            const AgentEvent event{std::to_string(sequence), model.id(), type,
                std::chrono::system_clock::now(), sequence, std::move(approval)};
            QCOMPARE(AgentStateMachine::apply(model, event), TransitionResult::Applied);
            pet.updateSession(model);
        };
        QCOMPARE(AgentStateMachine::beginConnecting(model), TransitionResult::Applied);
        pet.updateSession(model);
        QCOMPARE(visual->property("marker").toString(), QString("dots"));
        QTRY_VERIFY(visual->property("animationRunning").toBool());
        capture();
        send(AgentEventType::SessionConnected);
        QCOMPARE(visual->property("statusLabel").toString(), QString::fromUtf8("Active · idle"));
        QCOMPARE(visual->property("marker").toString(), QString("smile"));
        QVERIFY(visual->property("usesSprite").toBool());
        QTRY_VERIFY(visual->property("animationRunning").toBool());
        capture();
        const auto idleFrame = visual->property("spriteFrame").toInt();
        QTRY_VERIFY(visual->property("spriteFrame").toInt() != idleFrame);
        send(AgentEventType::TaskStarted);
        QCOMPARE(visual->property("marker").toString(), QString("bars"));
        QVERIFY(visual->property("usesSprite").toBool());
        const auto workingFrame = visual->property("spriteFrame").toInt();
        QTRY_VERIFY(visual->property("spriteFrame").toInt() != workingFrame);
        capture();
        QMetaObject::invokeMethod(motion, "triggered");
        QVERIFY(!visual->property("motionEnabled").toBool());
        QVERIFY(!visual->property("animationRunning").toBool());
        QCOMPARE(visual->property("statusLabel").toString(), QString("Working"));
        QMetaObject::invokeMethod(motion, "triggered");
        QTRY_VERIFY(visual->property("animationRunning").toBool());
        pet.hidePet();
        QTRY_VERIFY(!visual->property("animationRunning").toBool());
        pet.showPet();
        QTRY_VERIFY(visual->property("animationRunning").toBool());
        send(AgentEventType::TaskCompleted);
        QCOMPARE(visual->property("marker").toString(), QString("check"));
        capture();
        QTRY_VERIFY(!visual->property("animationRunning").toBool());
        send(AgentEventType::TaskStarted);
        send(AgentEventType::ApprovalRequested, ApprovalInfo{"request-1", std::nullopt});
        QCOMPARE(visual->property("statusLabel").toString(), QString("Approval needed"));
        QCOMPARE(visual->property("marker").toString(), QString("pause"));
        QVERIFY(visual->property("usesSprite").toBool());
        QTRY_VERIFY(visual->property("animationRunning").toBool());
        const auto approvalFrame = visual->property("spriteFrame").toInt();
        QTRY_VERIFY(visual->property("spriteFrame").toInt() != approvalFrame);
        capture();
        pet.updateSession(session("other"));
        QCOMPARE(visual->property("marker").toString(), QString("pause"));
        send(AgentEventType::ApprovalResolved, ApprovalInfo{"request-1", ApprovalDecision::Approve});
        QCOMPARE(visual->property("marker").toString(), QString("bars"));
        send(AgentEventType::SessionError);
        QCOMPARE(visual->property("marker").toString(), QString("cross"));
        capture();
        // Interrupt the one-shot error animation with disconnect and reconnect.
        send(AgentEventType::SessionDisconnected);
        QCOMPARE(visual->property("statusLabel").toString(), QString("Disconnected"));
        QVERIFY(!visual->property("animationRunning").toBool());
        QCOMPARE(AgentStateMachine::beginConnecting(model), TransitionResult::Applied);
        pet.updateSession(model);
        QTRY_VERIFY(visual->property("animationRunning").toBool());
        if (qEnvironmentVariableIsSet("DIMEPENNY_CAPTURE_UI"))
            QVERIFY(sheet.save("pet-visual-states.png"));
    }
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
