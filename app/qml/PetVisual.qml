import QtQuick

Item {
    id: visual
    required property string lifecycleState
    required property string appearanceKey
    property bool motionEnabled: true
    property bool windowVisible: true
    readonly property var styles: ({
        Unregistered: { label: "Not connected", color: "#64748b", fill: "#cbd5e1", mark: "line" },
        Connecting: { label: "Connecting", color: "#315cce", fill: "#a5bafa", mark: "dots" },
        Active: { label: "Active · idle", color: "#236345", fill: "#8fd3b0", mark: "smile" },
        Working: { label: "Working", color: "#5b3ba4", fill: "#b8a2ed", mark: "bars" },
        Completed: { label: "Completed", color: "#16644a", fill: "#86d6bc", mark: "check" },
        ApprovalNeeded: { label: "Approval needed", color: "#805200", fill: "#f2ca70", mark: "pause" },
        Error: { label: "Error", color: "#a3293c", fill: "#efa0ae", mark: "cross" },
        Disconnected: { label: "Disconnected", color: "#64748b", fill: "#cbd5e1", mark: "line" }
    })
    readonly property var style: styles[lifecycleState] || styles.Unregistered
    readonly property string statusLabel: style.label
    readonly property color accent: style.color
    readonly property string marker: style.mark
    readonly property bool animate: motionEnabled && windowVisible
    readonly property bool usesSprite: ["Active", "Working", "ApprovalNeeded"].indexOf(lifecycleState) >= 0
    readonly property var idleFrameSets: ({
        "orange": ["qrc:/pets/idle/orange/frame_1.png", "qrc:/pets/idle/orange/frame_2.png",
                   "qrc:/pets/idle/orange/frame_3.png", "qrc:/pets/idle/orange/frame_4.png"],
        "yellow": ["qrc:/pets/idle/yellow/frame_1.png", "qrc:/pets/idle/yellow/frame_2.png"],
        "pink": ["qrc:/pets/idle/pink/frame_1.png", "qrc:/pets/idle/pink/frame_2.png"],
        "green": ["qrc:/pets/idle/green/frame_1.png", "qrc:/pets/idle/green/frame_2.png"],
        "blue": ["qrc:/pets/idle/blue/frame_1.png", "qrc:/pets/idle/blue/frame_2.png"],
        "purple": ["qrc:/pets/idle/purple/frame_1.png", "qrc:/pets/idle/purple/frame_2.png"],
        "white": ["qrc:/pets/idle/white/frame_1.png", "qrc:/pets/idle/white/frame_2.png"],
        "blue-green": ["qrc:/pets/idle/blue-green/frame_1.png", "qrc:/pets/idle/blue-green/frame_2.png"],
        "grey": ["qrc:/pets/idle/grey/frame_1.png", "qrc:/pets/idle/grey/frame_2.png"],
        "golden": ["qrc:/pets/idle/golden/frame_1.png", "qrc:/pets/idle/golden/frame_2.png"]
    })
    readonly property string selectedColor: idleFrameSets[appearanceKey.toLowerCase()] ? appearanceKey.toLowerCase() : "orange"
    readonly property var idleFrames: idleFrameSets[selectedColor]
    readonly property int idleFrameCount: idleFrames.length
    readonly property int idleRestFrame: idleFrameCount === 2 ? 1 : 2
    readonly property var idleFrameDurations: idleFrameCount === 2 ? [180, 1400] : [120, 180, 1600, 130]
    readonly property var workingFrames: [
        "qrc:/pets/working/frame_1.png",
        "qrc:/pets/working/frame_2.png",
        "qrc:/pets/working/frame_3.png",
        "qrc:/pets/working/frame_4.png"
    ]
    readonly property var approvalFrames: [
        "qrc:/pets/approval/frame_1.png",
        "qrc:/pets/approval/frame_2.png",
        "qrc:/pets/approval/frame_3.png",
        "qrc:/pets/approval/frame_4.png"
    ]
    readonly property var spriteFrames: lifecycleState === "Active" ? idleFrames
                                              : lifecycleState === "ApprovalNeeded" ? approvalFrames
                                                                                   : workingFrames
    readonly property bool animationRunning: cycle.running || arrival.running || spriteCycle.running
    property real phase: 0
    property real arrivalPhase: 1
    property int spriteFrame: 0
    width: 150; height: 100

    function resetMotion() {
        arrival.stop()
        arrivalPhase = 1
        phase = 0
        spriteFrame = lifecycleState === "Active" ? idleRestFrame : 0
    }
    onLifecycleStateChanged: {
        resetMotion()
        if (animate && (lifecycleState === "Completed" || lifecycleState === "Error"))
            arrival.restart()
    }
    onAnimateChanged: { if (!animate) resetMotion() }
    onAppearanceKeyChanged: { if (lifecycleState === "Active") resetMotion() }
    NumberAnimation {
        id: cycle
        target: visual; property: "phase"; from: 0; to: 1
        duration: 1100
        loops: Animation.Infinite
        running: visual.animate && visual.lifecycleState === "Connecting"
    }
    NumberAnimation {
        id: arrival
        target: visual; property: "arrivalPhase"; from: 0; to: 1
        duration: 650
    }
    Timer {
        id: spriteCycle
        interval: visual.lifecycleState === "Active" ? visual.idleFrameDurations[visual.spriteFrame]
                                                     : visual.lifecycleState === "ApprovalNeeded" ? 220 : 260
        repeat: true
        running: visual.animate && visual.usesSprite
        onTriggered: {
            if (visual.lifecycleState === "ApprovalNeeded")
                visual.spriteFrame = visual.spriteFrame === 0 ? 1
                                     : visual.spriteFrame === 3 ? 1
                                     : visual.spriteFrame + 1
            else
                visual.spriteFrame = (visual.spriteFrame + 1) % visual.spriteFrames.length
        }
    }
    Item {
        id: sprite
        objectName: "petSprite"
        anchors.fill: parent
        visible: visual.usesSprite
        Repeater {
            model: visual.spriteFrames
            Image {
                required property int index
                required property string modelData
                anchors.fill: parent
                source: modelData
                fillMode: Image.PreserveAspectFit
                smooth: false
                mipmap: false
                cache: true
                visible: index === visual.spriteFrame
            }
        }
    }
    Item {
        id: paw
        visible: !visual.usesSprite
        width: 124; height: 92
        x: 13 + (visual.animate && visual.lifecycleState === "Error"
                 ? Math.sin(visual.arrivalPhase * Math.PI * 6) * 3 * (1 - visual.arrivalPhase) : 0)
        y: 5 - (visual.animate && visual.lifecycleState === "Working"
                ? (1 - Math.cos(visual.phase * Math.PI * 2)) * 2
                : visual.animate && visual.lifecycleState === "Completed"
                  ? Math.sin(visual.arrivalPhase * Math.PI) * 5 : 0)
        scale: visual.animate && visual.lifecycleState === "Active"
               ? 1 + Math.sin(visual.phase * Math.PI * 2) * 0.025 : 1
        Repeater {
            model: [{x: 0, y: 24, w: 24, h: 31}, {x: 28, y: 3, w: 25, h: 33},
                    {x: 69, y: 3, w: 25, h: 33}, {x: 99, y: 24, w: 24, h: 31},
                    {x: 27, y: 39, w: 69, h: 51}]
            Rectangle {
                required property var modelData
                x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h
                radius: width / 2
                color: visual.style.fill; border.color: visual.accent; border.width: 2
            }
        }
        Canvas {
            id: symbol
            x: 43; y: 49; width: 38; height: 30
            onPaint: {
                const c = getContext("2d")
                c.clearRect(0, 0, width, height)
                c.strokeStyle = visual.accent
                c.fillStyle = visual.accent
                c.lineWidth = 3; c.lineCap = "round"; c.lineJoin = "round"
                c.beginPath()
                switch (visual.marker) {
                case "check": c.moveTo(6, 15); c.lineTo(15, 23); c.lineTo(32, 7); c.stroke(); break
                case "pause": c.fillRect(10, 6, 6, 20); c.fillRect(23, 6, 6, 20); break
                case "cross": c.moveTo(10, 7); c.lineTo(29, 25); c.moveTo(29, 7); c.lineTo(10, 25); c.stroke(); break
                case "line": c.moveTo(9, 16); c.lineTo(29, 16); c.stroke(); break
                case "smile":
                    c.arc(11, 9, 2, 0, Math.PI * 2); c.fill()
                    c.beginPath(); c.arc(27, 9, 2, 0, Math.PI * 2); c.fill()
                    c.beginPath(); c.arc(19, 15, 10, 0.25, Math.PI - 0.25); c.stroke(); break
                }
            }
            Connections {
                target: visual
                function onMarkerChanged() { symbol.requestPaint() }
                function onAccentChanged() { symbol.requestPaint() }
            }
        }
        Row {
            x: 45; y: 60; spacing: 4
            visible: visual.marker === "dots"
            Repeater {
                model: 3
                Rectangle {
                    required property int index
                    width: 8; height: 8; radius: 4; color: visual.accent
                    opacity: !visual.animate || Math.floor(visual.phase * 3) === index ? 1 : 0.35
                }
            }
        }
        Row {
            x: 46; y: 53; height: 24; spacing: 4
            visible: visual.marker === "bars"
            Repeater {
                model: 3
                Rectangle {
                    required property int index
                    width: 7
                    height: visual.animate ? 10 + (Math.sin((visual.phase + index / 3) * Math.PI * 2) + 1) * 6 : 10 + index * 5
                    y: 24 - height; radius: 2; color: visual.accent
                }
            }
        }
    }
}
