// DockBar is the visible dock surface.
//
// Adaptive colour: when config.adaptiveColor is on, the background tints
// toward the dominant colour of the active window's icon.

import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root

    property string position: "bottom"
    property bool dockVisible:   true
    property bool dockAnimating: false   // true while the slide-in animation runs
    readonly property bool isHorizontal: position === "bottom" || position === "top"

    onDockVisibleChanged: console.log("[kdock autohide] DockBar.dockVisible →", dockVisible)

    implicitWidth:  isHorizontal ? itemRow.implicitWidth    + config.padding * 2
                                 : config.iconSize          + config.padding * 2
    implicitHeight: isHorizontal ? config.iconSize          + config.padding * 2
                                 : itemColumn.implicitHeight + config.padding * 2

    // ── Background ───────────────────────────────────────────────────────
    // Anchored to the dock EDGE. The window may be taller than the strip
    // (to allow hover-lift overflow) so the background only fills the
    // visual strip, not the entire window.
    Rectangle {
        readonly property int stripSize: config.iconSize + config.padding * 2

        anchors.left:   (isHorizontal || position === "left")  ? parent.left  : undefined
        anchors.right:  (isHorizontal || position === "right") ? parent.right : undefined
        anchors.top:    (position === "top"    || !isHorizontal) ? parent.top    : undefined
        anchors.bottom: (position === "bottom" || !isHorizontal) ? parent.bottom : undefined

        width:  isHorizontal ? parent.width : stripSize
        height: isHorizontal ? stripSize    : parent.height

        radius: config.backgroundRadius

        // Blend base background colour with adaptive tint when enabled
        color: {
            if (!config.adaptiveColor || taskTracker.activeAppId === "")
                return config.backgroundColor
            const tint = dockModel.iconDominantColor(
                dockModel.iconNameForApp(taskTracker.activeAppId))
            return Qt.rgba(
                tint.r * 0.25 + Qt.color(config.backgroundColor).r * 0.75,
                tint.g * 0.25 + Qt.color(config.backgroundColor).g * 0.75,
                tint.b * 0.25 + Qt.color(config.backgroundColor).b * 0.75,
                1.0)
        }
        opacity: config.backgroundOpacity

        Behavior on color   { ColorAnimation  { duration: 400 } }
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    // ── Mouse area for right-click dock menu ──────────────────────────────
    MouseArea {
        id: barMouse
        anchors.fill: parent
        hoverEnabled: false
        acceptedButtons: Qt.RightButton
        propagateComposedEvents: true

        onClicked: (event) => {
            if (event.button === Qt.RightButton) {
                const sp = barMouse.mapToGlobal(event.x, event.y)
                barContextMenu.mode = "dock"
                barContextMenu.openAt(sp.x, sp.y)
                event.accepted = true
            } else {
                event.accepted = false
            }
        }
    }

    // ── Scroll-to-zoom ───────────────────────────────────────────────────
    WheelHandler {
        id: scrollZoom
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => {
            if (event.angleDelta.y === 0) { event.accepted = false; return }
            const delta = event.angleDelta.y / 120
            const newSize = Math.max(config.scrollMinSize,
                            Math.min(config.scrollMaxSize,
                                     config.iconSize + delta * config.scrollStepPx))
            config.setIconSize(Math.round(newSize))
            event.accepted = true
        }
    }

    // ── Icon layout ───────────────────────────────────────────────────────
    Row {
        id: itemRow
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: position === "bottom" ? parent.bottom : undefined
        anchors.top:    position === "top"    ? parent.top    : undefined
        anchors.bottomMargin: position === "bottom" ? config.padding : 0
        anchors.topMargin:    position === "top"    ? config.padding : 0
        visible: isHorizontal
        spacing: config.spacing

        Repeater {
            model: isHorizontal ? dockModel : null
            delegate: DockItem {
                dockBar:       root
                position:      root.position
                dockAnimating: root.dockAnimating
            }
        }
    }

    Column {
        id: itemColumn
        anchors.verticalCenter: parent.verticalCenter
        anchors.left:  position === "left"  ? parent.left  : undefined
        anchors.right: position === "right" ? parent.right : undefined
        anchors.leftMargin:  position === "left"  ? config.padding : 0
        anchors.rightMargin: position === "right" ? config.padding : 0
        visible: !isHorizontal
        spacing: config.spacing

        Repeater {
            model: isHorizontal ? null : dockModel
            delegate: DockItem {
                dockBar:       root
                position:      root.position
                dockAnimating: root.dockAnimating
            }
        }
    }

    // ── Context menu ──────────────────────────────────────────────────────
    ContextMenu {
        id: barContextMenu
    }
}
