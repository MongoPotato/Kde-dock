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

    property bool _dockHovered: false
    property bool dockVisible: !config.autohide || _dockHovered

    // ── Hover detection for auto-hide ────────────────────────────────────
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        propagateComposedEvents: true
        onEntered: root._dockHovered = true
        onExited:  root._dockHovered = false
    }

    // ── Dock surface ─────────────────────────────────────────────────────
    DockBar {
        id: dockBar
        anchors.fill: parent
        position: config.position
        dockVisible: root.dockVisible

        transform: Translate {
            // Slide by the VISUAL dock thickness, not the full window height.
            // The window is taller than the visual strip to allow icon overflow.
            readonly property int baseThickness: config.iconSize + config.padding * 2
            x: (config.position === "left" || config.position === "right")
               ? (root.dockVisible ? 0
                                   : (config.position === "left" ? -baseThickness : baseThickness))
               : 0
            y: (config.position === "bottom" || config.position === "top")
               ? (root.dockVisible ? 0
                                   : (config.position === "bottom" ? baseThickness : -baseThickness))
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
