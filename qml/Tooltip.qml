// Tooltip renders as its own layer surface so it is never clipped by the thin
// dock strip and is never stacked underneath the dock (the dock sits on the
// TOP layer; an ordinary window would go behind it).
//
// It positions itself above/beside the icon using dockWindow.mapToScreen():
// Qt does not know where a layer surface is on screen, so mapToGlobal() would
// return dock-relative coordinates.

import QtQuick 2.15
import QtQuick.Window 2.15
import KDock 1.0

LayerPopup {
    id: root

    property string text: ""
    property string dockPosition: "bottom"
    property Item   parentItem: null

    // A tooltip is not a menu: it must not dismiss an open context menu.
    exclusive: false

    width:  label.implicitWidth + 20
    height: label.implicitHeight + 12

    function show() {
        if (!parentItem || text === "") return
        _reposition()
        visible = true
        fadeIn.restart()
    }

    function hide() {
        fadeOut.restart()
    }

    function _reposition() {
        const local = parentItem.mapToItem(null, 0, 0)
        const sp = dockWindow.mapToScreen(local.x, local.y)
        const pw = parentItem.width
        const ph = parentItem.height
        const gap = 8
        switch (dockPosition) {
        case "bottom":
            root.popupX = Math.round(sp.x + pw / 2 - width / 2)
            root.popupY = sp.y - height - gap
            break
        case "top":
            root.popupX = Math.round(sp.x + pw / 2 - width / 2)
            root.popupY = sp.y + ph + gap
            break
        case "left":
            root.popupX = sp.x + pw + gap
            root.popupY = Math.round(sp.y + ph / 2 - height / 2)
            break
        case "right":
            root.popupX = sp.x - width - gap
            root.popupY = Math.round(sp.y + ph / 2 - height / 2)
            break
        }
    }

    // Fade the CONTENT, not the window. Animating a Window's opacity makes
    // Qt's Wayland backend log "This plugin does not support setting window
    // opacity" on every frame of the animation, and does nothing visible.
    Item {
        id: content
        anchors.fill: parent
        opacity: 0

        NumberAnimation { id: fadeIn;  target: content; property: "opacity"; to: 1.0; duration: 120 }
        NumberAnimation {
            id: fadeOut; target: content; property: "opacity"; to: 0.0; duration: 100
            onStopped: if (content.opacity < 0.01) root.visible = false
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
}
