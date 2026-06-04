// ContextMenu.qml — right-click menu for the dock.
// mode: "app" when right-clicking a DockItem; "dock" for empty bar space.

import QtQuick 2.15
import QtQuick.Controls 2.15

Menu {
    id: root

    property string mode:  "dock"
    property string appId: ""

    readonly property int    appWindowCount: appId ? dockModel.windowCountForApp(appId) : 0
    readonly property bool   appIsRunning:   appId ? dockModel.isAppRunning(appId)      : false
    readonly property bool   appIsPinned:    appId ? dockModel.isAppPinned(appId)       : false
    readonly property string appDisplayName: appId ? dockModel.displayNameForApp(appId) : ""

    // ── App header ────────────────────────────────────────────────────────
    MenuItem {
        visible: root.mode === "app"
        enabled: false
        text: root.appDisplayName
              + (root.appIsRunning
                 ? "  ·  " + root.appWindowCount + (root.appWindowCount === 1 ? " window" : " windows")
                 : "  ·  Not running")
        font.bold: true
    }
    MenuSeparator { visible: root.mode === "app" }

    // ── App actions ───────────────────────────────────────────────────────
    MenuItem {
        visible: root.mode === "app"
        text: "Open new window"
        onTriggered: dockModel.launchApp(root.appId)
    }

    MenuItem {
        visible: root.mode === "app" && root.appIsPinned
        text: "Unpin from dock"
        onTriggered: dockModel.unpinApp(root.appId)
    }
    MenuItem {
        visible: root.mode === "app" && !root.appIsPinned
        text: "Pin to dock"
        onTriggered: dockModel.pinApp(root.appId)
    }

    MenuItem {
        visible: root.mode === "app" && root.appIsRunning
        text: "Close all windows"
        onTriggered: dockModel.closeApp(root.appId)
    }

    MenuSeparator { visible: root.mode === "app" }

    // ── Dock management ───────────────────────────────────────────────────
    MenuItem {
        text: "Manage dock apps…"
        onTriggered: settings.requestManageApps()
    }

    MenuItem {
        visible: root.mode === "dock"
        text: "Reload config"
        onTriggered: config.reload()
    }

    MenuSeparator {}

    MenuItem {
        text: "Dock settings…"
        onTriggered: settings.requestOpenSettings()
    }
}
