// ContextMenu.qml — right-click popup.
// Implemented as a Window so it appears outside the thin dock strip.
// Call openAt(screenX, screenY) after setting mode / appId.

import QtQuick 2.15
import QtQuick.Controls 2.15

Window {
    id: root

    property string mode:  "dock"
    property string appId: ""

    readonly property int    appWindowCount: appId !== "" ? dockModel.windowCountForApp(appId) : 0
    readonly property bool   appIsRunning:   appId !== "" ? dockModel.isAppRunning(appId)      : false
    readonly property bool   appIsPinned:    appId !== "" ? dockModel.isAppPinned(appId)       : false
    readonly property string appDisplayName: appId !== "" ? dockModel.displayNameForApp(appId) : ""

    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    width:  240
    height: menuCol.implicitHeight + 10

    // Dismiss when focus moves elsewhere (click outside)
    onActiveChanged: if (!active) visible = false

    // Open above the click point, clamped to the primary screen.
    function openAt(screenX, screenY) {
        visible = true
        Qt.callLater(positionSelf, screenX, screenY)
    }

    function positionSelf(sx, sy) {
        const scr = Qt.application.screens[0]
        let px = sx - 8
        let py = sy - root.height - 8
        if (px + root.width > scr.virtualX + scr.width)  px = scr.virtualX + scr.width - root.width - 8
        if (px < scr.virtualX)                            px = scr.virtualX + 8
        if (py < scr.virtualY)                            py = sy + 8
        root.x = px
        root.y = py
        raise()
        requestActivate()
    }

    // ── Visual shell ─────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: "#22223a"
        radius: 9
        border.color: "#555577"
        border.width: 1

        Column {
            id: menuCol
            anchors { left: parent.left; right: parent.right; top: parent.top }
            topPadding:    6
            bottomPadding: 6
            spacing: 0

            // ── App header ────────────────────────────────────────────────
            Item {
                visible: root.mode === "app"
                width: parent.width
                height: visible ? 44 : 0
                Text {
                    anchors { left: parent.left; right: parent.right
                              leftMargin: 14; rightMargin: 14; verticalCenter: parent.verticalCenter }
                    text: root.appDisplayName
                          + (root.appIsRunning
                             ? "  ·  " + root.appWindowCount
                               + (root.appWindowCount === 1 ? " window" : " windows")
                             : "  ·  Not running")
                    color: "#99bbff"
                    font.bold: true
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            Rectangle {
                visible: root.mode === "app"
                width: parent.width - 16; height: visible ? 1 : 0
                anchors.horizontalCenter: parent.horizontalCenter
                color: "#444466"
            }

            // ── App actions ───────────────────────────────────────────────
            CMenuItem {
                label: "Open new window"
                visible: root.mode === "app"
                onTriggered: dockModel.launchApp(root.appId)
            }
            CMenuItem {
                label: "Pin to dock"
                visible: root.mode === "app" && !root.appIsPinned
                onTriggered: dockModel.pinApp(root.appId)
            }
            CMenuItem {
                label: "Unpin from dock"
                visible: root.mode === "app" && root.appIsPinned
                onTriggered: dockModel.unpinApp(root.appId)
            }
            CMenuItem {
                label: "Close all windows"
                labelColor: "#ff9090"
                visible: root.mode === "app" && root.appIsRunning
                onTriggered: dockModel.closeApp(root.appId)
            }
            Rectangle {
                visible: root.mode === "app"
                width: parent.width - 16; height: visible ? 1 : 0
                anchors.horizontalCenter: parent.horizontalCenter
                color: "#444466"
            }

            // ── Dock-level actions ────────────────────────────────────────
            CMenuItem {
                label: "Manage dock apps…"
                onTriggered: settings.requestManageApps()
            }
            CMenuItem {
                label: "Reload config"
                visible: root.mode === "dock"
                onTriggered: config.reload()
            }
            Rectangle {
                width: parent.width - 16; height: 1
                anchors.horizontalCenter: parent.horizontalCenter
                color: "#444466"
            }
            CMenuItem {
                label: "Dock settings…"
                onTriggered: settings.requestOpenSettings()
            }
        }
    }

    Shortcut {
        context: Qt.WindowShortcut
        sequence: "Escape"
        onActivated: root.visible = false
    }

    // ── Inline menu-entry component ───────────────────────────────────────
    component CMenuItem: Item {
        id: entry
        property string label:      ""
        property string labelColor: "white"
        signal triggered()

        width:  parent ? parent.width : 240
        height: visible ? 36 : 0

        Rectangle {
            anchors { fill: parent; leftMargin: 4; rightMargin: 4 }
            color:  entryMouse.containsMouse ? "#3a3a60" : "transparent"
            radius: 5
        }
        Text {
            anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 14 }
            text:  entry.label
            color: entry.labelColor
            font.pixelSize: 13
        }
        MouseArea {
            id: entryMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape:  Qt.PointingHandCursor
            onClicked: { root.visible = false; entry.triggered() }
        }
    }
}
