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
//   dockWindow          → LayerShellWindow*

import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root

    // ── Auto-hide state ──────────────────────────────────────────────────
    property bool _dockHovered:   false
    property bool _dockAnimating: false
    property bool dockVisible:    !config.autohide || _dockHovered

    Component.onCompleted: {
        // Match the real Wayland surface to the initial visibility so a
        // dock that starts auto-hidden doesn't block the screen edge.
        if (typeof dockWindow !== "undefined")
            dockWindow.setRevealed(root.dockVisible)
    }

    onDockVisibleChanged: {
        console.log("[kdock autohide] dockVisible →", dockVisible,
                    "  autohide:", config.autohide, "  _dockHovered:", _dockHovered)
        if (dockVisible) {
            // Lock out hover-on-icon animations while the dock slides in
            root._dockAnimating = true
            animDoneTimer.restart()
            hideShrinkTimer.stop()
            // Grow the real surface back to full size immediately so the
            // icons have somewhere to land before the slide-in finishes.
            if (typeof dockWindow !== "undefined") dockWindow.setRevealed(true)
        } else {
            hideTimer.stop()
            // Wait for the slide-out animation to finish before shrinking the
            // real surface — shrinking too early would clip the dock mid-animation.
            hideShrinkTimer.restart()
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

    // Shrink the real Wayland surface to a reveal-strip once the 220ms
    // slide-out transform has had time to finish, so the surface itself
    // never visibly snaps out from under the still-animating content.
    Timer {
        id: hideShrinkTimer
        interval: 230
        repeat:   false
        onTriggered: {
            if (typeof dockWindow !== "undefined") dockWindow.setRevealed(false)
        }
    }

    // ── Hover detection for auto-hide ─────────────────────────────────────
    // HoverHandler (not MouseArea) so it doesn't compete for exclusive hover
    // ownership with the per-icon MouseAreas in DockItem — a plain MouseArea
    // here would get spurious Exited/Entered toggles every time the cursor
    // crossed onto/off of an icon, making the auto-hide dock flicker.
    HoverHandler {
        id: autohideHover
        onHoveredChanged: {
            if (hovered) {
                console.log("[kdock autohide] window ENTERED")
                hideTimer.stop()
                root._dockHovered = true
            } else {
                console.log("[kdock autohide] window EXITED")
                if (config.autohide) hideTimer.restart()
            }
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
