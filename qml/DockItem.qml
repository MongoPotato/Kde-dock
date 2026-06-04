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
//   dockBar      → parent DockBar item  (for magnification)
//   position     → dock edge            (string)
//
// Hover state machine:
//   "normal"  → y=0, glow invisible, scale = magnifyScale
//   "hovered" → y=-liftPx, glow visible, scale = magnifyScale * scaleBoost

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

    property int iconVersion: 0

    readonly property bool isHorizontal: position === "bottom" || position === "top"

    // Magnification scale driven by cursor distance to this item's centre
    property real magnifyScale: 1.0

    Connections {
        target: dockBar
        function onCursorXChanged() { updateScale() }
        function onCursorYChanged() { updateScale() }
    }

    Connections {
        target: iconThemeDetector
        function onThemeChanged() { root.iconVersion++ }
    }

    function updateScale() {
        const cx = root.x + root.width  / 2
        const cy = root.y + root.height / 2
        magnifyScale = dockBar.scaleForItem(cx, cy)
    }

    implicitWidth:  config.iconSize * magnifyScale + config.iconBgPadding * 2 + 4
    implicitHeight: config.iconSize * magnifyScale + config.iconBgPadding * 2 + 4

    Behavior on magnifyScale { NumberAnimation { duration: 80 } }

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
        width:  config.iconSize * root.magnifyScale + config.iconBgPadding * 2
        height: width
        radius: width / 2
        color: config.hoverGlowColor === "auto" ? "#5294e2" : config.hoverGlowColor
        opacity: 0
    }

    // ── Container that slides up on hover ───────────────────────────────
    Item {
        id: iconContainer
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter:   parent.verticalCenter
        width:  root.implicitWidth
        height: root.implicitHeight
        y: 0

        // ── Per-icon background pill / circle / squircle ─────────────────
        Rectangle {
            id: iconBg
            anchors.centerIn: iconImage
            width:  config.iconSize * root.magnifyScale + config.iconBgPadding * 2
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
            width:  config.iconSize * root.magnifyScale * (root.state === "hovered" ? config.hoverScaleBoost : 1.0)
            height: width
            source: "image://kdock/" + root.iconName + "?v=" + root.iconVersion
            fillMode: Image.PreserveAspectFit
            smooth: true
            asynchronous: true

            Behavior on width { NumberAnimation { duration: 100 } }

            // Bounce animation when urgent
            SequentialAnimation on y {
                id: bounceAnim
                running: isUrgent
                loops: Animation.Infinite
                NumberAnimation { to: -12; duration: 200; easing.type: Easing.OutQuad }
                NumberAnimation { to:   0; duration: 200; easing.type: Easing.InQuad  }
                PauseAnimation  { duration: 600 }
            }
        }

        // ── Running indicator dot ────────────────────────────────────────
        Rectangle {
            visible: isRunning && config.runningIndicatorVisible
            color:  config.runningIndicatorColor
            width:  config.runningIndicatorSize
            height: config.runningIndicatorSize
            radius: width / 2
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: isHorizontal ? parent.bottom : undefined
            anchors.right:  isHorizontal ? undefined      : parent.right
            anchors.bottomMargin: isHorizontal ? 2 : 0
            anchors.rightMargin:  isHorizontal ? 0 : 2
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
                dockModel.launchApp(root.appId)
            } else {
                contextMenu.mode  = "app"
                contextMenu.appId = root.appId
                contextMenu.popup()
            }
        }

        onEntered: {
            root.state = "hovered"
            tooltip.show()
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
