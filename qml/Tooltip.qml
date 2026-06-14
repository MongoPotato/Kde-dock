// Tooltip renders as a separate OS window so it is never clipped by the
// thin dock layer-shell surface. It positions itself above/beside the icon
// using mapToGlobal so screen coordinates are always correct.

import QtQuick 2.15
import QtQuick.Window 2.15

Window {
    id: root

    property string text: ""
    property string dockPosition: "bottom"
    property Item   parentItem: null

    flags:  Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color:  "transparent"
    width:  label.implicitWidth + 20
    height: label.implicitHeight + 12

    function show() {
        if (!parentItem || text === "") return
        _reposition()
        visible = true
        raise()
        opacity = 0
        fadeIn.restart()
    }

    function hide() {
        fadeOut.restart()
    }

    function _reposition() {
        const sp = parentItem.mapToGlobal(0, 0)
        const pw = parentItem.width
        const ph = parentItem.height
        const gap = 8
        switch (dockPosition) {
        case "bottom":
            x = Math.round(sp.x + pw / 2 - width / 2)
            y = sp.y - height - gap
            break
        case "top":
            x = Math.round(sp.x + pw / 2 - width / 2)
            y = sp.y + ph + gap
            break
        case "left":
            x = sp.x + pw + gap
            y = Math.round(sp.y + ph / 2 - height / 2)
            break
        case "right":
            x = sp.x - width - gap
            y = Math.round(sp.y + ph / 2 - height / 2)
            break
        }
    }

    NumberAnimation { id: fadeIn;  target: root; property: "opacity"; to: 1.0; duration: 120 }
    NumberAnimation {
        id: fadeOut; target: root; property: "opacity"; to: 0.0; duration: 100
        onStopped: if (root.opacity < 0.01) root.visible = false
    }

    Rectangle {
        anchors.fill: parent
        color:  "#dd101018"
        radius: 6
        border.color: "#555577"
        border.width: 1

        Text {
            id: label
            anchors.centerIn: parent
            text: root.text
            color: "white"
            font.pixelSize: 13
        }
    }
}
