// DockBar is the visible dock surface.
//
// Parabolic magnification:
//   scale = 1 + (maxExtraScale * max(0, 1 - dist/magnetRadius))
//   where dist = distance from cursor to item centre.
//
// Adaptive colour: when config.adaptiveColor is on, the background tints
// toward the dominant colour of the active window's icon.

import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root

    property string position: "bottom"
    readonly property bool isHorizontal: position === "bottom" || position === "top"

    property real cursorX: -1000
    property real cursorY: -1000

    function scaleForItem(itemCentreX, itemCentreY) {
        if (!config.magnify) return 1.0
        const dist = Math.sqrt(
            Math.pow(cursorX - itemCentreX, 2) +
            Math.pow(cursorY - itemCentreY, 2)
        )
        const extra = config.magnifyScale - 1.0
        return 1.0 + extra * Math.max(0, 1 - dist / config.magnifyRadius)
    }

    implicitWidth:  isHorizontal ? itemRow.implicitWidth    + config.padding * 2
                                 : config.iconSize          + config.padding * 2
    implicitHeight: isHorizontal ? config.iconSize          + config.padding * 2
                                 : itemColumn.implicitHeight + config.padding * 2

    // ── Background ───────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
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

    // ── Cursor tracker for magnification ─────────────────────────────────
    MouseArea {
        id: barMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        propagateComposedEvents: true

        onPositionChanged: (mouse) => {
            root.cursorX = mouse.x
            root.cursorY = mouse.y
        }
        onExited: {
            root.cursorX = -1000
            root.cursorY = -1000
        }
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
        anchors.centerIn: parent
        visible: isHorizontal
        spacing: config.spacing

        Repeater {
            model: isHorizontal ? dockModel : null
            delegate: DockItem {
                dockBar:  root
                position: root.position
            }
        }
    }

    Column {
        id: itemColumn
        anchors.centerIn: parent
        visible: !isHorizontal
        spacing: config.spacing

        Repeater {
            model: isHorizontal ? null : dockModel
            delegate: DockItem {
                dockBar:  root
                position: root.position
            }
        }
    }

    // ── Context menu ──────────────────────────────────────────────────────
    ContextMenu {
        id: barContextMenu
    }
}
