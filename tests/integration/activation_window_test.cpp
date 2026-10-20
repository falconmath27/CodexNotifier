#include "activation_window.h"
#include "pet_window.h"
#include <QQuickItem>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>
class ActivationWindowTest : public QObject {
    Q_OBJECT
private slots:
    void concurrentPets()
    {
        using namespace dimepenny;
        QTemporaryDir project; QVERIFY(project.isValid());
        ActivationWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QList<ipc::SimulatedBridge*> bridges;
        QList<PetWindow*> pets;
        QStringList ids, appearances, names;
        // Capture each bridge/window as it is created rather than relying on map order.
        for (int i = 0; i < 10; ++i) {
            window.configure(project.path(), "PowerShell", i == 0 ? 0 : 1);
            QVERIFY(window.canActivate());
            window.activate();
            QTRY_COMPARE(window.sessions().size(), i + 1);
            ipc::SimulatedBridge* added = nullptr;
            for (auto* bridge : window.findChildren<ipc::SimulatedBridge*>())
                if (!bridges.contains(bridge)) added = bridge;
            QVERIFY(added); bridges.append(added);
            PetWindow* addedPet = nullptr;
            for (auto* candidate : QGuiApplication::topLevelWindows())
                if (auto* pet = qobject_cast<PetWindow*>(candidate); pet && !pets.contains(pet))
                    addedPet = pet;
            QVERIFY(addedPet);
            pets.append(addedPet);
            QTRY_COMPARE(addedPet->state(), QString("Active"));
            ids.append(addedPet->sessionId());
            appearances.append(addedPet->appearance());
            names.append(addedPet->agentName());
        }
        QCOMPARE(QSet<QString>(ids.begin(), ids.end()).size(), 10);
        QCOMPARE(QSet<QString>(appearances.begin(), appearances.end()).size(), 10);
        QCOMPARE(QSet<QString>(names.begin(), names.end()).size(), 10);
        window.activate(); // The eleventh activation cannot create a bridge or pet.
        QCOMPARE(window.capacityUsed(), 10);
        QCOMPARE(window.findChildren<ipc::SimulatedBridge*>().size(), 10);
        std::vector<std::unique_ptr<QSignalSpy>> updates;
        for (auto* pet : pets) updates.push_back(std::make_unique<QSignalSpy>(pet, &PetWindow::changed));
        QVERIFY(bridges[0]->sendEvent("TaskStarted"));
        QTRY_COMPARE(pets[0]->state(), QString("Working"));
        for (int i = 1; i < 10; ++i) {
            QCOMPARE(pets[i]->state(), QString("Active"));
            QCOMPARE(updates[static_cast<std::size_t>(i)]->count(), 0);
        }
        QVERIFY(bridges[0]->replayLastEvent());
        QTest::qWait(40);
        QCOMPARE(updates[0]->count(), 1); // Duplicate does not repaint.
        QVERIFY(bridges[0]->sendEvent("TaskCompleted"));
        QTRY_COMPARE(pets[0]->state(), QString("Completed"));
        for (auto& spy : updates) spy->clear();

        QSignalSpy listUpdates(&window, &ActivationWindow::changed);
        int pulses = 0;
        QTimer heartbeat;
        heartbeat.setInterval(1);
        connect(&heartbeat, &QTimer::timeout, &window, [&] { ++pulses; });
        heartbeat.start();
        // 400 valid events, interleaved across ten authenticated sockets.
        for (int round = 0; round < 20; ++round)
            for (auto* bridge : bridges) {
                QVERIFY(bridge->sendEvent("TaskStarted"));
                QVERIFY(bridge->sendEvent("TaskCompleted"));
            }
        // Distinct final states expose accidental routing to the wrong pet.
        for (int i = 0; i < 10; ++i) {
            if (i % 3 == 0) QVERIFY(bridges[i]->sendEvent("TaskStarted"));
            if (i % 3 == 1) QVERIFY(bridges[i]->sendEvent("SessionError"));
        }
        for (int i = 0; i < 10; ++i)
            QTRY_COMPARE_WITH_TIMEOUT(pets[i]->state(),
                i % 3 == 0 ? QString("Working") : i % 3 == 1 ? QString("Error") : QString("Completed"), 5000);
        QVERIFY(pulses > 0);
        QVERIFY(listUpdates.count() < 100); // Not one list rebuild per event.
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(pets[i]->sessionId(), ids[i]);
            QCOMPARE(pets[i]->appearance(), appearances[i]);
            QCOMPARE(pets[i]->agentName(), names[i]);
        }
        // One reconnect preserves the same window and identity; peers stay untouched.
        for (auto& spy : updates) spy->clear();
        pets[0]->hidePet();
        bridges[0]->disconnectFromServer();
        QTRY_COMPARE(pets[0]->state(), QString("Disconnected"));
        window.reconnectSession(ids[0]);
        QTRY_COMPARE(pets[0]->state(), QString("Active"));
        QVERIFY(!pets[0]->isVisible());
        QCOMPARE(pets[0]->appearance(), appearances[0]);
        for (int i = 1; i < 10; ++i)
            QCOMPARE(updates[static_cast<std::size_t>(i)]->count(), 0);
        window.close(); // Pending presentation timers must not reopen pets.
        QTest::qWait(30);
        for (auto* pet : pets) QVERIFY(!pet->isVisible());
    }
    void activationAndReconnect_data()
    {
        QTest::addColumn<bool>("dimeFirst");
        QTest::newRow("dime-first") << true;
        QTest::newRow("pennies-first") << false;
    }
    void activationAndReconnect()
    {
        QFETCH(bool, dimeFirst);
        QTemporaryDir project; QVERIFY(project.isValid());
        dimepenny::ActivationWindow window;
        QCOMPARE(window.status(), QQuickView::Ready);
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* path = window.rootObject()->findChild<QObject*>("projectDirectory");
        auto* companion = window.rootObject()->findChild<QObject*>("companionType");
        auto* button = window.rootObject()->findChild<QQuickItem*>("activateButton");
        QVERIFY(path && companion && button);
        QVERIFY(!window.canActivate());
        path->setProperty("text", project.path() + "/missing");
        QVERIFY(!window.canActivate());
        path->setProperty("text", project.path()); QVERIFY2(window.canActivate(), qPrintable(window.validation() + " " + window.connection()));
        QSet<QString> expectedNames;
        for (int i = 0; i < 10; ++i) {
            const bool isDime = dimeFirst ? i == 0 : i == 9;
            companion->setProperty("currentIndex", isDime ? 0 : 1);
            const auto expectedName = isDime ? QString("Dime") : QString("Penny%1").arg(dimeFirst ? i : i + 1);
            QCOMPARE(window.nextName(), expectedName);
            expectedNames.insert(expectedName);
            QVERIFY(window.canActivate());
            QTRY_VERIFY(button->isEnabled());
            QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
            QVERIFY(!window.canActivate());
            QTRY_VERIFY_WITH_TIMEOUT(window.connection().startsWith("Connected:"), 5000);
            QCOMPARE(window.capacityUsed(), i + 1);
            QTRY_COMPARE(window.sessions().size(), i + 1);
            QSet<QString> actualNames;
            for (const auto& entry : window.sessions())
                actualNames.insert(entry.toMap().value("name").toString());
            QCOMPARE(actualNames, expectedNames);
            if (isDime || (!dimeFirst && i == 8)) {
                QVERIFY(!window.canActivate());
                window.activate();
                QCOMPARE(window.capacityUsed(), i + 1);
            }
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
        QSet<QString> colors;
        for (auto* candidate : QGuiApplication::topLevelWindows())
            if (auto* p = qobject_cast<dimepenny::PetWindow*>(candidate))
                colors.insert(p->appearance());
        QCOMPARE(colors.size(), 10);
        QCOMPARE(window.pennyCount(), 9);
        QVERIFY(window.dimeReserved());
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
        QVERIFY(!window.canActivate());
        QSet<QString> reconnectedNames;
        for (const auto& entry : window.sessions())
            reconnectedNames.insert(entry.toMap().value("name").toString());
        QCOMPARE(reconnectedNames, expectedNames);
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
