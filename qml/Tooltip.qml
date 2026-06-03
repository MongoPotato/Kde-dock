// Tooltip is intentionally kept as a plain QML Rectangle rather than
// Qt.ToolTip so we have full control over its position relative to
// the layer-shell window geometry.
//
// Bindings:
//   text          → display string (app name)
//   dockPosition  → "bottom"|"top"|"left"|"right" (determines placement side)
//   parentItem    → the DockItem that owns this tooltip

import QtQuick 2.15

Item {
    id: root

    property string text: ""
    property string dockPosition: "bottom"
    property Item   parentItem: null

    // Offset between tooltip edge and icon edge
    readonly property int gap: 8

    // Tooltip is parented to the window root so it can escape the icon bounds
    parent: root.parentItem ? root.parentItem.Window.contentItem : null

    visible: false
    opacity: 0

    function show() {
        visible = true
        fadeIn.restart()
    }

    function hide() {
        fadeOut.restart()
    }

    NumberAnimation { id: fadeIn;  target: root; property: "opacity"; to: 1; duration: 120 }
    NumberAnimation {
        id: fadeOut
        target: root
        property: "opacity"
        to: 0
        duration: 120
        onStopped: if (root.opacity === 0) root.visible = false
    }

    // Position relative to parentItem within the shared Window
    function reposition() {
        if (!parentItem) return
        const mapped = parentItem.mapToItem(null, 0, 0)
        const pw = parentItem.width
        const ph = parentItem.height

        switch (dockPosition) {
        case "bottom":
            x = mapped.x + pw / 2 - bg.width / 2
            y = mapped.y - bg.height - gap
            break
        case "top":
            x = mapped.x + pw / 2 - bg.width / 2
            y = mapped.y + ph + gap
            break
        case "left":
            x = mapped.x + pw + gap
            y = mapped.y + ph / 2 - bg.height / 2
            break
        case "right":
            x = mapped.x - bg.width - gap
            y = mapped.y + ph / 2 - bg.height / 2
            break
        }
    }

    onVisibleChanged: if (visible) reposition()

    Rectangle {
        id: bg
        color: "#cc000000"
        radius: 6
        width:  label.width  + 16
        height: label.height + 10

        Text {
            id: label
            anchors.centerIn: parent
            text: root.text
            color: "white"
            font.pixelSize: 13
        }
    }
}
