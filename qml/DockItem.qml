// DockItem represents one launcher slot.
// The "?v=<iconVersion>" cache-buster is incremented by IconProvider
// whenever the custom icon file changes, forcing QML to re-request
// the image without a full dock restart.
//
// Bindings (all from DockBar Repeater required properties):
//   appId        → role from DockModel  (string)
//   displayName  → role from DockModel  (string)
//   iconName     → role from DockModel  (string, resolved icon name)
//   isPinned     → role from DockModel  (bool)
//   isRunning    → role from DockModel  (bool)
//   windowCount  → role from DockModel  (int)
//   isUrgent     → role from DockModel  (bool)
//   dockBar      → parent DockBar item
//   position     → dock edge            (string)
//
// Hover state machine:
//   "normal"  → y=0, glow invisible
//   "hovered" → y=-liftPx, glow visible

import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root

    required property string appId
    required property string displayName
    required property string iconName
    required property bool   isPinned
    required property bool   isRunning
    required property int    windowCount
    required property bool   isUrgent
    required property Item   dockBar
    required property string position
    required property bool   dockAnimating

    property int iconVersion: 0

    readonly property bool isHorizontal: position === "bottom" || position === "top"

    Connections {
        target: dockBar
        function onDockVisibleChanged() {
            if (!dockBar.dockVisible) {
                root.state = "normal"
                tooltip.hide()
                clickBounceAnim.stop()
                iconContainer.clickBounceY = 0
            }
        }
    }

    Connections {
        target: iconThemeDetector
        function onThemeChanged() { root.iconVersion++ }
    }

    // Fixed item size (no magnification)
    implicitWidth:  config.iconSize + config.iconBgPadding * 2 + 4
    implicitHeight: config.iconSize + config.iconBgPadding * 2 + 4

    // ── Hover state machine ──────────────────────────────────────────────
    // State "normal": y offset = 0, glow opacity = 0
    // State "hovered": y = -liftPx, glow visible with configured opacity
    states: [
        State {
            name: "normal"
            PropertyChanges { target: iconContainer; y: 0 }
            PropertyChanges { target: hoverGlow;    opacity: 0 }
        },
        State {
            name: "hovered"
            PropertyChanges { target: iconContainer; y: -(config.hoverLiftPx) }
            PropertyChanges { target: hoverGlow;    opacity: config.hoverGlowOpacity }
        }
    ]
    transitions: [
        Transition {
            from: "*"; to: "hovered"
            NumberAnimation { properties: "y,opacity"; duration: 120; easing.type: Easing.OutCubic }
        },
        Transition {
            from: "hovered"; to: "normal"
            NumberAnimation { properties: "y,opacity"; duration: 200; easing.type: Easing.InOutQuad }
        }
    ]

    state: "normal"

    // ── Glow ring behind icon ────────────────────────────────────────────
    Rectangle {
        id: hoverGlow
        anchors.centerIn: parent
        width:  config.iconSize + config.iconBgPadding * 2
        height: width
        radius: width / 2
        color: config.hoverGlowColor === "auto" ? "#5294e2" : config.hoverGlowColor
        opacity: 0
    }

    // ── Container that slides up on hover ───────────────────────────────
    // verticalCenter anchor is intentionally absent: the states below
    // animate y to achieve the lift effect; anchors.verticalCenter would
    // suppress any y change made via PropertyChanges.
    //
    // clickBounceY is a separate transform offset so it stacks cleanly on
    // top of the state-driven y without interfering with hover transitions.
    Item {
        id: iconContainer
        anchors.horizontalCenter: parent.horizontalCenter
        width:  root.implicitWidth
        height: root.implicitHeight
        y: 0

        property real clickBounceY: 0
        transform: Translate { y: iconContainer.clickBounceY }

        SequentialAnimation {
            id: clickBounceAnim
            NumberAnimation { target: iconContainer; property: "clickBounceY"
                              to: -22; duration: 110; easing.type: Easing.OutQuad }
            NumberAnimation { target: iconContainer; property: "clickBounceY"
                              to:   6; duration:  80; easing.type: Easing.InQuad }
            NumberAnimation { target: iconContainer; property: "clickBounceY"
                              to:   0; duration: 140; easing.type: Easing.OutBounce }
        }

        // ── Per-icon background pill / circle / squircle ─────────────────
        Rectangle {
            id: iconBg
            anchors.centerIn: iconImage
            width:  config.iconSize + config.iconBgPadding * 2
            height: width
            radius: iconBgRadius()
            color:  config.iconBgColor
            opacity: config.iconBgOpacity
            visible: config.iconBgShape !== "none"
            border.color: config.iconBgBorderColor
            border.width: config.iconBgBorderWidth

            function iconBgRadius() {
                switch (config.iconBgShape) {
                case "circle":   return width / 2
                case "pill":     return config.iconBgRadius
                case "squircle": return config.iconSize * 0.30
                default:         return 0
                }
            }
        }

        // ── Icon image ───────────────────────────────────────────────────
        Image {
            id: iconImage
            anchors.centerIn: parent
            width:  config.iconSize * (root.state === "hovered" ? config.hoverScaleBoost : 1.0)
            height: width
            source: "image://kdock/" + root.iconName + "?v=" + root.iconVersion
            // Request at high resolution so the icon stays crisp on HiDPI screens.
            sourceSize.width:  256
            sourceSize.height: 256
            fillMode: Image.PreserveAspectFit
            smooth: true
            asynchronous: true

            Behavior on width { NumberAnimation { duration: 100 } }

            // Bounce offset for urgent state — uses transform so anchors.centerIn
            // keeps working while still allowing vertical displacement.
            property real bounceOffset: 0
            transform: Translate { y: iconImage.bounceOffset }

            SequentialAnimation on bounceOffset {
                id: bounceAnim
                running: isUrgent
                loops: Animation.Infinite
                NumberAnimation { to: -12; duration: 200; easing.type: Easing.OutQuad }
                NumberAnimation { to:   0; duration: 200; easing.type: Easing.InQuad  }
                PauseAnimation  { duration: 600 }
            }
        }

        // ── Running indicator dot (top of icon, pulses gently) ──────────
        Rectangle {
            id: runningDot
            visible: isRunning && config.runningIndicatorVisible
            color:  config.runningIndicatorColor
            width:  config.runningIndicatorSize
            height: config.runningIndicatorSize
            radius: width / 2

            // Horizontal dock: centre above the icon; vertical dock: to the side
            anchors.horizontalCenter: isHorizontal ? parent.horizontalCenter : undefined
            anchors.verticalCenter:   isHorizontal ? undefined               : parent.verticalCenter
            anchors.top:   isHorizontal ? parent.top : undefined
            anchors.right: isHorizontal ? undefined  : parent.right
            anchors.topMargin:   isHorizontal ? 3 : 0
            anchors.rightMargin: isHorizontal ? 0 : 3

            // Soft pulsing glow so the dot is easy to spot at a glance
            SequentialAnimation on opacity {
                running: isRunning
                loops:   Animation.Infinite
                NumberAnimation { from: 1.0; to: 0.4; duration: 900; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.4; to: 1.0; duration: 900; easing.type: Easing.InOutSine }
            }
        }
    }

    // ── Mouse interaction ────────────────────────────────────────────────
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onClicked: (event) => {
            if (event.button === Qt.LeftButton) {
                clickBounceAnim.restart()
                // activateApp: focus existing window, or launch if not running.
                // Guard with typeof in case binary is older than QML.
                if (typeof dockModel.activateApp === "function")
                    dockModel.activateApp(root.appId)
                else
                    dockModel.launchApp(root.appId)
                dockModel.clearUrgency(root.appId)
            } else {
                const sp = mouse.mapToGlobal(event.x, event.y)
                contextMenu.mode  = "app"
                contextMenu.appId = root.appId
                contextMenu.openAt(sp.x, sp.y)
            }
        }

        onEntered: {
            // Don't enter hover state while the dock is sliding in — the
            // icon is still in motion and the animation looks jerky.
            if (!root.dockAnimating) {
                root.state = "hovered"
                tooltip.show()
            }
        }
        onExited: {
            root.state = "normal"
            tooltip.hide()
        }
    }

    // ── Tooltip ──────────────────────────────────────────────────────────
    Tooltip {
        id: tooltip
        text: root.displayName
        dockPosition: root.position
        parentItem: root
    }

    // ── Context menu (shared ContextMenu component) ──────────────────────
    ContextMenu {
        id: contextMenu
    }
}
