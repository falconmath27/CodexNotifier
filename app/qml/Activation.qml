import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtQuick.Dialogs

Pane {
    id: root
    width: 1000; height: 760
    font.family: "Segoe UI"
    font.pixelSize: 14
    padding: 28
    palette.highlight: "#315cce"
    property string selectedId: ""
    background: Rectangle { color: "#f3f5fa" }
    function configure() {
        if (!agent || !folder || !terminal || !appearance) return
        backend.configure(agent.text, folder.text, terminal.currentText, appearance.currentIndex)
    }
    Component.onCompleted: configure()
    Connections {
        target: backend
        function onSelectSession(id) { root.selectedId = id }
    }
    FolderDialog {
        id: picker
        title: "Choose your project folder"
        onAccepted: folder.text = backend.localPath(selectedFolder)
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 20
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 5
                Label { text: "Dime&Penny"; font.pixelSize: 30; font.bold: true; color: "#18243a" }
                Label { text: "A little company for every session."; color: "#58657a" }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: backend.capacityUsed + " / 10 sessions"
                padding: 12; color: "#315cce"; font.bold: true
                background: Rectangle { color: "#e6ecff"; radius: 12 }
            }
        }
        Label {
            text: "Simulation mode · Try the desktop pets with a simulated session."
            color: "#58657a"; Layout.fillWidth: true; wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            spacing: 24
            Pane {
                Layout.preferredWidth: 320; Layout.fillHeight: true
                padding: 20
                background: Rectangle { color: "white"; radius: 16; border.color: "#dde3ed" }
                ScrollView {
                    anchors.fill: parent
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 10
                        Label { text: "New session"; font.pixelSize: 21; font.bold: true; color: "#18243a" }
                        Label { text: "Give your next project a companion."; color: "#58657a"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Item { height: 6 }
                        Label { text: "Agent name" }
                        TextField {
                            id: agent; objectName: "agentName"
                            Layout.fillWidth: true; placeholderText: "Frontend helper"
                            Accessible.name: "Agent name"
                            onTextChanged: root.configure()
                        }
                        Label { text: "Project folder" }
                        TextField {
                            id: folder; objectName: "projectDirectory"; Layout.fillWidth: true
                            placeholderText: "Choose an existing folder"
                            Accessible.name: "Project folder"
                            onTextChanged: root.configure()
                        }
                        Button { text: "Browse folders…"; Layout.fillWidth: true; onClicked: picker.open() }
                        Label { text: "Terminal" }
                        ComboBox {
                            id: terminal; objectName: "terminalType"; Layout.fillWidth: true
                            model: ["PowerShell", "Command Prompt", "Git Bash", "WSL"]
                            Accessible.name: "Terminal type"
                            onCurrentTextChanged: root.configure()
                        }
                        Label { text: "Pet appearance" }
                        ComboBox {
                            id: appearance; objectName: "petAppearance"; Layout.fillWidth: true
                            model: ["Cat", "Dog", "Fox", "Rabbit", "Owl", "Panda", "Bear", "Otter", "Penguin", "Turtle"]
                            Accessible.name: "Pet appearance"
                            onCurrentIndexChanged: root.configure()
                        }
                        Label {
                            objectName: "validationStatus"
                            text: backend.validation; color: backend.canActivate ? "#236345" : "#58657a"
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                        }
                        Button {
                            objectName: "activateButton"
                            text: "Activate session"; highlighted: true
                            Layout.fillWidth: true; enabled: backend.canActivate
                            onClicked: backend.activate()
                        }
                        Label {
                            text: "Each pet stays reserved for its session. No terminal is launched in simulation mode."
                            font.pixelSize: 12; color: "#58657a"
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
                Label { text: "Your companions"; font.pixelSize: 21; font.bold: true; color: "#18243a" }
                Label {
                    visible: backend.sessions.length === 0
                    text: "Your first companion starts here.\nActivate a session to bring a pet onto your desktop."
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#58657a"
                }
                ListView {
                    id: list; objectName: "sessions"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    model: backend.sessions; spacing: 10; clip: true
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Pane {
                        required property var modelData
                        width: list.width; padding: 14
                        implicitHeight: sessionLayout.implicitHeight + 28
                        background: Rectangle {
                            color: "white"; radius: 12
                            border.color: root.selectedId === modelData.sessionId ? "#315cce" : "#dde3ed"
                        }
                        ColumnLayout {
                            id: sessionLayout
                            anchors.fill: parent; spacing: 8
                            Label { text: modelData.name; textFormat: Text.PlainText; font.bold: true; color: "#18243a"; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { text: modelData.appearance + " · " + modelData.terminal; color: "#58657a"; Layout.fillWidth: true; elide: Text.ElideRight }
                            Label { text: modelData.state; color: "#315cce"; font.bold: true }
                            Flow {
                                Layout.fillWidth: true; spacing: 6
                                Button { text: "Show pet"; onClicked: { root.selectedId = modelData.sessionId; backend.showPet(modelData.sessionId) } }
                                Button { text: "Details"; onClicked: backend.showDetails(modelData.sessionId) }
                                Button { text: "Reconnect"; enabled: modelData.canReconnect; onClicked: backend.reconnectSession(modelData.sessionId) }
                            }
                        }
                    }
                }
            }
        }
        Label {
            objectName: "connectionStatus"; text: backend.connection
            textFormat: Text.PlainText; color: "#58657a"; Layout.fillWidth: true; wrapMode: Text.WordWrap
        }
    }
}
