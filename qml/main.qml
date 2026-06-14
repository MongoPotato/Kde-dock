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

    // ── Auto-hide state ──────────────────────────────────────────────────
    property bool _dockHovered:   false
    property bool _dockAnimating: false
    property bool dockVisible:    !config.autohide || _dockHovered

    onDockVisibleChanged: {
        console.log("[kdock autohide] dockVisible →", dockVisible,
                    "  autohide:", config.autohide, "  _dockHovered:", _dockHovered)
        if (dockVisible) {
            // Lock out hover-on-icon animations while the dock slides in
            root._dockAnimating = true
            animDoneTimer.restart()
        } else {
            hideTimer.stop()
        }
    }

    // 1-second delay before hiding so a brief cursor-leave doesn't flicker
    Timer {
        id: hideTimer
        interval: 1000
        repeat:   false
        onTriggered: {
            console.log("[kdock autohide] hide timer fired → hiding dock")
            root._dockHovered = false
        }
    }

    // Unlock icon hover-state after the slide-in animation completes (~220 ms)
    Timer {
        id: animDoneTimer
        interval: 280
        repeat:   false
        onTriggered: {
            console.log("[kdock autohide] slide-in animation done — hover enabled")
            root._dockAnimating = false
        }
    }

    // ── Hover detection for auto-hide ─────────────────────────────────────
    // Full-window area so the cursor touches the screen edge to reveal.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        propagateComposedEvents: true
        onEntered: {
            console.log("[kdock autohide] window ENTERED")
            hideTimer.stop()
            root._dockHovered = true
        }
        onExited: {
            console.log("[kdock autohide] window EXITED")
            if (config.autohide) hideTimer.restart()
        }
    }

    // ── Dock surface ─────────────────────────────────────────────────────
    DockBar {
        id: dockBar
        anchors.fill: parent
        position:      config.position
        dockVisible:   root.dockVisible
        dockAnimating: root._dockAnimating

        transform: Translate {
            // Slide by the VISUAL dock thickness (icon strip height).
            // Window is taller than the strip to accommodate hover-lift overflow.
            readonly property int baseThickness: config.iconSize + config.padding * 2
            x: (config.position === "left" || config.position === "right")
               ? (root.dockVisible ? 0
                                   : (config.position === "left" ? -baseThickness : baseThickness))
               : 0
            y: (config.position === "bottom" || config.position === "top")
               ? (root.dockVisible ? 0
                                   : (config.position === "bottom" ? baseThickness : -baseThickness))
               : 0

            Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
            Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        }
    }

    // ── App management window (separate OS window) ────────────────────────
    AppPickerPanel {
        id: appPickerPanel
        visible: false
    }

    // ── Visual settings panel ─────────────────────────────────────────────
    SettingsPanel {
        id: settingsPanel
    }

    // ── Signal routing ────────────────────────────────────────────────────
    Connections {
        target: settings
        function onOpenSettingsRequested()  { settingsPanel.open() }
        function onManageAppsRequested()    { appPickerPanel.visible = true; appPickerPanel.raise() }
    }
}
