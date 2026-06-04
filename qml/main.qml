// main.qml — root QML loaded by LayerShellWindow (QQuickView).
// The root must be Item, not Window: LayerShellWindow IS the window.
// Width and height are managed by the layer-shell configure callback
// (or X11 fallback setGeometry); QQuickView::SizeRootObjectToView keeps
// this Item in sync with the window automatically.
//
// Context properties available:
//   config              → ConfigWatcher*       (position, iconSize, autohide, …)
//   settings            → SettingsController*  (validated mutators + signals)
//   dockModel           → DockModel*           (list of dock entries)
//   taskTracker         → TaskTracker*         (running window state)
//   iconThemeDetector   → IconThemeDetector*   (KDE theme name)

import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root

    // When autohide is off the dock is always visible.
    // When autohide is on it starts hidden and reveals on hover.
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

    // ── Settings panel — opened via SettingsController signal ────────────
    SettingsPanel {
        id: settingsPanel
    }

    Connections {
        target: settings
        function onOpenSettingsRequested() { settingsPanel.open() }
    }
}
