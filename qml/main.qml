// main.qml — root QML loaded by LayerShellWindow (QQuickView).
// The root must be Item, not Window: LayerShellWindow IS the dock window.
//
// Context properties:
//   config              → ConfigWatcher*
//   settings            → SettingsController*
//   dockModel           → DockModel*
//   taskTracker         → TaskTracker*
//   iconThemeDetector   → IconThemeDetector*
//   appLibrary          → AppLibrary*

import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root

    property bool dockVisible: !config.autohide

    // ── Hover detection for auto-hide ────────────────────────────────────
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        propagateComposedEvents: true
        onEntered: if (config.autohide) root.dockVisible = true
        onExited:  if (config.autohide) root.dockVisible = false
    }

    // ── Dock surface ─────────────────────────────────────────────────────
    DockBar {
        id: dockBar
        anchors.fill: parent
        position: config.position

        transform: Translate {
            x: (config.position === "left" || config.position === "right")
               ? (root.dockVisible ? 0
                                   : (config.position === "left" ? -root.width : root.width))
               : 0
            y: (config.position === "bottom" || config.position === "top")
               ? (root.dockVisible ? 0
                                   : (config.position === "bottom" ? root.height : -root.height))
               : 0

            Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
        }
    }

    // ── App management window (separate OS window) ───────────────────────
    AppPickerPanel {
        id: appPickerPanel
        visible: false
    }

    // ── Visual settings panel ────────────────────────────────────────────
    SettingsPanel {
        id: settingsPanel
    }

    // ── Signal routing ───────────────────────────────────────────────────
    Connections {
        target: settings
        function onOpenSettingsRequested()  { settingsPanel.open() }
        function onManageAppsRequested()    { appPickerPanel.visible = true; appPickerPanel.raise() }
    }
}
