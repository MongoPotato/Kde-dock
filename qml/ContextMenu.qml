// ContextMenu.qml — right-click menu for the dock.
// Property "mode":  "app" | "dock"
// Property "appId": the XDG app ID of the right-clicked item (app mode only)
//
// Design rules:
//   - Labels start with a verb when the item performs an action.
//   - Labels use sentence case, never title case.
//   - Separator lines group related actions; max 3 groups per menu.
//   - "Dock settings" is always last — it does not belong to the app.
//   - Items that don't apply in the current state use visible: false,
//     not enabled: false — a greyed-out item is confusing for actions
//     that simply don't apply.

import QtQuick 2.15
import QtQuick.Controls 2.15

Menu {
    id: root

    // "app" for a right-click on a DockItem; "dock" for a right-click on empty space
    property string mode:  "dock"
    property string appId: ""

    // Resolved display properties for the focused app
    readonly property int    appWindowCount: appId ? dockModel.windowCountForApp(appId) : 0
    readonly property bool   appIsRunning:   appId ? dockModel.isAppRunning(appId)      : false
    readonly property bool   appIsPinned:    appId ? dockModel.isAppPinned(appId)        : false
    readonly property string appDisplayName: appId ? dockModel.displayNameForApp(appId)  : ""

    // ── App header (mode == "app" only) ──────────────────────────────────
    MenuItem {
        visible: root.mode === "app"
        enabled: false
        text: root.appDisplayName + (root.appIsRunning
              ? "  ·  " + root.appWindowCount + (root.appWindowCount === 1 ? " window open" : " windows open")
              : "  ·  Not running")
        font.bold: true
    }
    MenuSeparator { visible: root.mode === "app" }

    // ── App actions (mode == "app") ───────────────────────────────────────
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

    // ── Dock-only actions (mode == "dock") ────────────────────────────────
    MenuItem {
        visible: root.mode === "dock"
        text: "Add application…"
        onTriggered: dockModel.addAppDialog()
    }

    MenuItem {
        visible: root.mode === "dock"
        text: "Reload config"
        onTriggered: config.reload()
    }

    MenuSeparator { visible: root.mode === "dock" }

    // ── Always shown at bottom ────────────────────────────────────────────
    MenuItem {
        text: "Dock settings…"
        onTriggered: settings.requestOpenSettings()
    }
}
