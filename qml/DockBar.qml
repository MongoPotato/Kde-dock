// DockBar is the visible dock surface.
// The parabolic magnification formula (used by Latte and macOS):
//   scale = 1 + (maxExtraScale * Math.max(0, 1 - dist / magnetRadius))
// where dist is pixels from cursor to item centre.
// This is computed in a JS function called from each DockItem's
// Connections to this bar's cursor properties.
//
// Scroll-to-zoom uses WheelHandler (Qt 6) which correctly handles both
// mouse wheel clicks (angleDelta.y = ±120) and high-resolution touchpad
// events (fractional angleDelta values).
//
// Bindings:
//   config.position          → layout direction
//   config.iconSize          → base icon size
//   config.padding           → bar padding
//   config.spacing           → spacing between icons
//   config.magnify           → enable/disable parabolic zoom
//   config.magnifyScale      → max scale factor
//   config.magnifyRadius     → magnification field radius in px
//   config.backgroundColor   → bar background colour
//   config.backgroundOpacity → bar background opacity
//   config.backgroundRadius  → bar corner radius
//   config.scrollStepPx      → icon size change per wheel click
//   config.scrollMinSize     → scroll minimum icon size
//   config.scrollMaxSize     → scroll maximum icon size
//   dockModel                → ListView model

import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root

    property string position: "bottom"
    readonly property bool isHorizontal: position === "bottom" || position === "top"

    // Current cursor position relative to this item (updated by overlay MouseArea)
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
        color:   config.backgroundColor
        opacity: config.backgroundOpacity
        radius:  config.backgroundRadius

        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    // ── Cursor tracker for magnification ─────────────────────────────────
    MouseArea {
        id: barMouse
        anchors.fill: parent
        hoverEnabled: true
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
                barContextMenu.mode = "dock"
                barContextMenu.popup()
            }
            event.accepted = false
        }
    }

    // ── Scroll-to-zoom handler ───────────────────────────────────────────
    // WheelHandler is preferred over MouseArea.onWheel in Qt 6 because it
    // correctly handles high-resolution touchpad scroll events and gives us
    // the angleDelta in both X and Y axes.
    //
    // We use angleDelta.y: positive = scroll up = icon grows.
    // Each "click" of a standard scroll wheel = 120 units.
    // We map 120 units → config.scrollStepPx pixels of icon size change.
    WheelHandler {
        id: scrollZoom
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => {
            if (event.angleDelta.y === 0) {
                event.accepted = false
                return
            }
            const delta = event.angleDelta.y / 120
            const newSize = Math.max(config.scrollMinSize,
                            Math.min(config.scrollMaxSize,
                                     config.iconSize + delta * config.scrollStepPx))
            config.setIconSize(newSize)
            event.accepted = true
        }
    }

    // ── Icon layout ───────────────────────────────────────────────────────
    // Row/Column are used directly so there is no ambiguity about how many
    // items fit per line (Grid's column/row 0 semantics vary across Qt versions).
    Row {
        id: itemRow
        anchors.centerIn: parent
        visible: isHorizontal
        spacing: config.spacing

        Repeater {
            model: isHorizontal ? dockModel : null

            DockItem {
                required property string appId
                required property string displayName
                required property string iconName
                required property bool isPinned
                required property bool isRunning
                required property int  windowCount
                required property bool isUrgent

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

            DockItem {
                required property string appId
                required property string displayName
                required property string iconName
                required property bool isPinned
                required property bool isRunning
                required property int  windowCount
                required property bool isUrgent

                dockBar:  root
                position: root.position
            }
        }
    }

    // ── Dock-level context menu ───────────────────────────────────────────
    ContextMenu {
        id: barContextMenu
    }
}
