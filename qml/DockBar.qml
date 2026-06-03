// DockBar is the visible dock surface.
// The parabolic magnification formula (used by Latte and macOS):
//   scale = 1 + (maxExtraScale * Math.max(0, 1 - dist / magnetRadius))
// where dist is pixels from cursor to item centre.
// This is computed in a JS function called from each DockItem's
// MouseArea.onPositionChanged via a Connections to the bar's MouseArea.
//
// Bindings:
//   config.position          → layout direction (Row / Column)
//   config.iconSize          → base icon size
//   config.padding           → bar padding
//   config.spacing           → spacing between icons
//   config.magnify           → enable/disable parabolic zoom
//   config.magnifyScale      → max scale factor
//   config.magnifyRadius     → magnification field radius in px
//   config.backgroundColor   → bar background colour
//   config.backgroundOpacity → bar background opacity
//   config.backgroundRadius  → bar corner radius
//   dockModel                → ListView model

import QtQuick 2.15

Item {
    id: root

    property string position: "bottom"
    readonly property bool isHorizontal: position === "bottom" || position === "top"

    // Current cursor position relative to this item (updated by inner MouseArea)
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

    // Size the bar to fit its content
    implicitWidth:  isHorizontal ? itemRow.implicitWidth  + config.padding * 2
                                 : config.iconSize        + config.padding * 2
    implicitHeight: isHorizontal ? config.iconSize        + config.padding * 2
                                 : itemRow.implicitHeight + config.padding * 2

    // Background
    Rectangle {
        anchors.fill: parent
        color: config.backgroundColor
        opacity: config.backgroundOpacity
        radius: config.backgroundRadius

        // Smooth add/remove of the entire bar
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    // Overlay MouseArea to track cursor for magnification
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
    }

    // Icon layout — Row for horizontal, Column for vertical docks
    Grid {
        id: itemRow
        anchors.centerIn: parent
        columns: isHorizontal ? -1 : 1  // -1 = unlimited columns (use rows)
        rows:    isHorizontal ? 1 : -1
        spacing: config.spacing

        Repeater {
            model: dockModel

            DockItem {
                required property string appId
                required property string displayName
                required property string iconName
                required property bool isPinned
                required property bool isRunning
                required property int windowCount
                required property bool isUrgent

                dockBar: root
                position: root.position
            }
        }
    }
}
