// ContextMenu.qml — right-click popup.
//
// LayerPopup, not Window: a plain QML Window declared inside the dock gets the
// dock's layer surface as its transientParent, and KWin then stacked the menu
// with the dock's layer instead of above it — the menu opened UNDERNEATH the
// dock (issue #2). LayerPopup gives the menu its own layer surface on the
// OVERLAY layer, above the dock whatever layer the dock itself is on. It falls
// back to an ordinary window where layer-shell isn't available.
//
// Call openAt(screenX, screenY) after setting mode / appId.

import QtQuick 2.15
import QtQuick.Controls 2.15
import KDock 1.0

LayerPopup {
    id: root

    property string mode:  "dock"
    property string appId: ""

    readonly property int    appWindowCount: appId !== "" ? dockModel.windowCountForApp(appId) : 0
    readonly property bool   appIsRunning:   appId !== "" ? dockModel.isAppRunning(appId)      : false
    readonly property bool   appIsPinned:    appId !== "" ? dockModel.isAppPinned(appId)       : false
    readonly property string appDisplayName: appId !== "" ? dockModel.displayNameForApp(appId) : ""

    // flags and color are set by LayerShellPopup — which of them applies
    // depends on whether the layer-shell path or the fallback is in use.
    width:  240
    height: menuCol.implicitHeight + 10

    // Has this menu ever actually held keyboard focus? The dock is a
    // layer-shell surface with keyboard interactivity set to NONE, so
    // requestActivate() on a menu opened from it is not guaranteed to be
    // granted. Dismissing on any inactive state meant that when activation
    // never arrived the menu was torn down the moment it appeared — the
    // "really tricky to get the option menu up" flash.
    property bool _everActive: false

    // Whether the cursor has ever reached the menu since it opened. Before it
    // has, the user is still travelling towards the menu and it must not go
    // anywhere; once they have been on it and left, they are done with it.
    property bool _pointerVisited: false

    onPopupActiveChanged: {
        if (popupActive) {
            _everActive = true
            return
        }
        // Focus-loss dismissal only where focus is actually meaningful. On the
        // layer-shell path focus arrives and leaves on its own as the
        // compositor shuffles it around, and treating that as a click-outside
        // shut the menu again before it could be read.
        if (!usingLayerShell && _everActive && visible)
            visible = false
    }

    // NOTE: no onVisibleChanged handler here. DockBar and DockItem attach one
    // at the instantiation site to track open menus, and a use-site handler
    // silently replaces one written in the component — so _everActive is reset
    // in openAt() instead, where it can't be overridden.

    // Open above the click point, clamped to the screen the click was on.
    function openAt(screenX, screenY) {
        _everActive = false
        _pointerVisited = false
        // Position BEFORE showing. Showing first and positioning in a
        // callLater made the menu appear at its previous coordinates for a
        // frame and jump, which on its own is enough to lose a click.
        positionSelf(screenX, screenY)
        visible = true
        Qt.callLater(_settle, screenX, screenY)
    }

    // Deferred so the window is actually mapped before we ask to be raised
    // and activated, and so the re-clamp sees the height this mode's entries
    // settled on rather than the previous mode's.
    function _settle(sx, sy) {
        positionSelf(sx, sy)
        raise()
        requestActivate()
    }

    // The screen the click happened on — not always screens[0] on a
    // multi-monitor desktop, where clamping to the wrong screen could park
    // the menu off the edge of the one the user is looking at.
    function _screenAt(sx, sy) {
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; ++i) {
            const s = screens[i]
            if (sx >= s.virtualX && sx < s.virtualX + s.width
             && sy >= s.virtualY && sy < s.virtualY + s.height)
                return s
        }
        return screens[0]
    }

    function positionSelf(sx, sy) {
        const scr = _screenAt(sx, sy)
        const gap = 8
        let px = sx - gap
        let py = sy - root.height - gap

        // Push the menu entirely clear of the dock, rather than merely above
        // the click point. Clicking an icon puts the click inside the dock, so
        // "above the click" still left the lower entries overlapping the dock
        // strip — and whichever of the two the compositor draws on top, half
        // the menu was unusable. Measuring from the dock's own rectangle means
        // the menu never shares pixels with it.
        const d = dockWindow.dockScreenRect
        switch (config.position) {
        case "top":   py = Math.max(py, d.y + d.height + gap); break
        case "left":  px = Math.max(px, d.x + d.width  + gap); break
        case "right": px = Math.min(px, d.x - root.width - gap); break
        default:      py = Math.min(py, d.y - root.height - gap); break
        }

        // Then keep it on the screen.
        if (px + root.width > scr.virtualX + scr.width)  px = scr.virtualX + scr.width - root.width - gap
        if (px < scr.virtualX)                            px = scr.virtualX + gap
        if (py + root.height > scr.virtualY + scr.height) py = scr.virtualY + scr.height - root.height - gap
        if (py < scr.virtualY)                            py = scr.virtualY + gap

        // popupX/popupY, not x/y: a layer surface is placed by margins, so
        // Qt's own window position means nothing to it. The fallback path
        // mirrors these onto x/y.
        root.popupX = px
        root.popupY = py
    }

    // With focus-loss dismissal gone on the layer-shell path, the cursor is
    // what closes the menu. Two very different situations, so two very
    // different timeouts:
    //
    //   not yet visited — the user is still moving towards the menu, or just
    //                     reading it from where they are. Give them a long
    //                     while; the previous flat 4 s read as the menu
    //                     vanishing before they could react.
    //   visited, now away — they have used it and moved on. Close promptly.
    //
    // Opening another menu closes this one too (LayerShellPopup exclusivity),
    // as does choosing an entry or pressing Escape.
    HoverHandler {
        id: menuHover
        onHoveredChanged: if (hovered) root._pointerVisited = true
    }

    Timer {
        id: closeGuard
        interval: root._pointerVisited ? 1500 : 15000
        running:  root.visible && !menuHover.hovered
        repeat:   false
        onTriggered: root.visible = false
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
