// DockItem represents one launcher slot.
// The "?v=<iconVersion>" cache-buster is incremented by IconProvider
// whenever the custom icon file changes, forcing QML to re-request
// the image without a full dock restart.
//
// Bindings:
//   appId        → role from DockModel  (string)
//   displayName  → role from DockModel  (string)
//   iconName     → role from DockModel  (string, resolved icon name)
//   isPinned     → role from DockModel  (bool)
//   isRunning    → role from DockModel  (bool)
//   windowCount  → role from DockModel  (int)
//   isUrgent     → role from DockModel  (bool)
//   dockBar      → parent DockBar item  (for magnification)
//   position     → dock edge            (string)

import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root

    // Properties set by DockBar's Repeater
    required property string appId
    required property string displayName
    required property string iconName
    required property bool   isPinned
    required property bool   isRunning
    required property int    windowCount
    required property bool   isUrgent
    required property Item   dockBar
    required property string position

    // Icon version for cache-busting (incremented externally if needed)
    property int iconVersion: 0

    readonly property bool isHorizontal: position === "bottom" || position === "top"

    // Magnification scale driven by cursor distance
    property real magnifyScale: 1.0

    // Update scale every frame the cursor moves (via dockBar signal)
    Connections {
        target: dockBar
        function onCursorXChanged() { updateScale() }
        function onCursorYChanged() { updateScale() }
    }

    function updateScale() {
        const cx = root.x + root.width  / 2
        const cy = root.y + root.height / 2
        magnifyScale = dockBar.scaleForItem(cx, cy)
    }

    implicitWidth:  config.iconSize * magnifyScale + 4
    implicitHeight: config.iconSize * magnifyScale + 4

    Behavior on magnifyScale { NumberAnimation { duration: 80 } }

    // ── Icon image ──────────────────────────────────────────────────────────
    Image {
        id: icon
        anchors.centerIn: parent
        width:  config.iconSize * root.magnifyScale
        height: config.iconSize * root.magnifyScale
        source: "image://kdock/" + root.iconName + "?v=" + root.iconVersion
        fillMode: Image.PreserveAspectFit
        smooth: true
        asynchronous: true

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

    // ── Running indicator dot ───────────────────────────────────────────────
    Rectangle {
        visible: isRunning && config.runningIndicatorVisible
        color: config.runningIndicatorColor
        width:  config.runningIndicatorSize
        height: config.runningIndicatorSize
        radius: width / 2
        anchors.horizontalCenter: parent.horizontalCenter
        // Position below for bottom/top docks, right-edge for left/right
        anchors.bottom: isHorizontal ? parent.bottom : undefined
        anchors.right:  isHorizontal ? undefined      : parent.right
        anchors.bottomMargin: isHorizontal ? 2 : 0
        anchors.rightMargin:  isHorizontal ? 0 : 2
    }

    // ── Mouse interaction ───────────────────────────────────────────────────
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onClicked: (event) => {
            if (event.button === Qt.LeftButton) {
                dockModel.launchApp(root.appId)
            } else {
                contextMenu.popup()
            }
        }

        onEntered: tooltip.show()
        onExited:  tooltip.hide()
    }

    // ── Tooltip ─────────────────────────────────────────────────────────────
    Tooltip {
        id: tooltip
        text: root.displayName
        dockPosition: root.position
        parentItem: root
    }

    // ── Context menu ────────────────────────────────────────────────────────
    Menu {
        id: contextMenu

        MenuItem {
            text: "Launch new window"
            onTriggered: dockModel.launchApp(root.appId)
        }
        MenuItem {
            text: root.isPinned ? "Unpin from dock" : "Pin to dock"
            onTriggered: {
                if (root.isPinned)
                    dockModel.unpinApp(root.appId)
                else
                    dockModel.pinApp(root.appId)
            }
        }
        MenuItem {
            text: "Close all windows"
            enabled: root.isRunning
            onTriggered: dockModel.closeApp(root.appId)
        }
    }
}
