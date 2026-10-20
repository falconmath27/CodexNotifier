import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

Item {
    id: root
    width: 200; height: 190
    focus: true
    Accessible.name: pet.agentName + " desktop pet"
    Accessible.description: pet.details
    Keys.onPressed: event => {
        const step = (event.modifiers & Qt.ShiftModifier) ? 1 : 10
        switch (event.key) {
        case Qt.Key_Left: pet.nudge(-step, 0); break
        case Qt.Key_Right: pet.nudge(step, 0); break
        case Qt.Key_Up: pet.nudge(0, -step); break
        case Qt.Key_Down: pet.nudge(0, step); break
        case Qt.Key_Return: case Qt.Key_Enter: pet.showDetails(); break
        case Qt.Key_Menu: menu.popup(); break
        case Qt.Key_F10:
            if (event.modifiers & Qt.ShiftModifier) menu.popup()
            else return
            break
        default: return
        }
        event.accepted = true
    }
    PetVisual {
        id: visual; objectName: "petVisual"
        anchors.horizontalCenter: parent.horizontalCenter
        lifecycleState: pet.state
        appearanceKey: pet.appearance
        windowVisible: pet.visible
    }
    Rectangle {
        x: 5; y: 103; width: 190; height: 82; radius: 16
        color: "#f8faff"; border.color: "#cbd5e8"
        Column {
            anchors.fill: parent; anchors.margins: 10; spacing: 3
            Text { width: parent.width; text: pet.agentName; textFormat: Text.PlainText; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter; font.family: "Segoe UI"; font.pixelSize: 14; font.bold: true; color: "#18243a" }
            Text { width: parent.width; text: pet.appearance === "orange" ? "Dime" : "Penny"; horizontalAlignment: Text.AlignHCenter; font.family: "Segoe UI"; font.pixelSize: 12; color: "#58657a" }
            Text { width: parent.width; text: visual.statusLabel; horizontalAlignment: Text.AlignHCenter; font.family: "Segoe UI"; font.pixelSize: 13; color: visual.accent }
        }
        Rectangle { anchors.fill: parent; anchors.margins: 3; radius: 13; color: "transparent"; border.color: "#315cce"; visible: root.activeFocus }
    }
    MouseArea {
        id: pointer; anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton; hoverEnabled: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onPressed: mouse => {
            root.forceActiveFocus()
            if (mouse.button === Qt.LeftButton) { const p = mapToGlobal(mouse.x, mouse.y); pet.beginDrag(p.x, p.y) }
        }
        onPositionChanged: mouse => {
            if (pressedButtons & Qt.LeftButton) { const p = mapToGlobal(mouse.x, mouse.y); pet.dragTo(p.x, p.y) }
        }
        onClicked: mouse => { if (mouse.button === Qt.RightButton) menu.popup() }
        onDoubleClicked: mouse => { if (mouse.button === Qt.LeftButton) pet.showDetails() }
        ToolTip {
            visible: pointer.containsMouse && !pointer.pressed && !menu.visible
            delay: 700
            contentItem: Text { text: pet.details; textFormat: Text.PlainText; color: "#18243a"; font.pixelSize: 12; wrapMode: Text.Wrap; width: 360 }
        }
    }
    Menu {
        id: menu; objectName: "petMenu"; popupType: Popup.Window
        MenuItem { objectName: "petDetails"; text: "Session details"; onTriggered: pet.showDetails() }
        MenuItem { text: "Open project folder"; onTriggered: { if (!pet.openProject()) { errorText.text = "Could not open the project folder."; detailsWindow.show() } } }
        MenuItem { objectName: "petReconnect"; text: "Reconnect session"; enabled: pet.state === "Disconnected"; onTriggered: pet.reconnect() }
        MenuSeparator {}
        MenuItem { objectName: "petMotion"; text: "Animate pet"; checkable: true; checked: visual.motionEnabled; onTriggered: visual.motionEnabled = !visual.motionEnabled }
        MenuItem { objectName: "petOnTop"; text: "Always on top"; checkable: true; checked: pet.onTop; onTriggered: pet.setOnTop(!pet.onTop) }
        MenuItem { text: "Reset position"; onTriggered: pet.resetPosition() }
        MenuItem { text: "Show activation window"; onTriggered: pet.activationRequested() }
        MenuItem { objectName: "petHide"; text: "Hide pet (keep session connected)"; onTriggered: pet.hidePet() }
    }
    Connections {
        target: pet
        function onDetailsRequested() { errorText.text = ""; detailsWindow.show(); detailsWindow.raise(); detailsWindow.requestActivate() }
        function onVisibleChanged() { if (!pet.visible) { detailsWindow.hide(); menu.close() } }
    }
    Window {
        id: detailsWindow; objectName: "petSessionDetails"
        width: 520; height: 400; minimumWidth: 360; minimumHeight: 260
        title: "Session details — Dime&Penny"
        flags: Qt.Dialog | Qt.WindowStaysOnTopHint
        color: "#f3f5fa"
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 20; spacing: 12
            Label { id: errorText; visible: text.length > 0; color: "#a32b36"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                TextArea { objectName: "detailsText"; text: pet.details; textFormat: TextEdit.PlainText; readOnly: true; wrapMode: TextEdit.Wrap; font.family: "Segoe UI"; font.pixelSize: 14; Accessible.name: "Session details" }
            }
            Button { text: "Close"; Layout.alignment: Qt.AlignRight; onClicked: detailsWindow.close() }
        }
    }
}
