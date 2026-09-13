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

    // ── Auto-hide ─────────────────────────────────────────────────────────
    //
    // The dock window is deliberately TALLER than the visible icon strip so
    // that hover-lifted icons and click bounces aren't clipped. Auto-hide
    // therefore can't just ask "is the cursor over the window?" — it has to
    // ask "is the cursor over the ICON BAR?", and it must only hide once the
    // cursor has genuinely left that band and stayed out for the full delay.
    //
    // Everything below is written so that no single stray hover event can
    // hide the dock: the decision is re-derived from pointer position via
    // evaluateAutohide(), and re-checked once more when the timer fires.

    property bool pointerInWindow: false
    property bool pointerInZone:   false
    readonly property bool pointerOnDock: pointerInWindow && pointerInZone

    // A menu or panel opened from the dock keeps it up: the cursor is over
    // that window, not the dock, but the user is plainly still using it.
    readonly property bool menuOpen: dockBar.openMenuCount > 0
                                  || settingsPanel.visible
                                  || appPickerPanel.visible

    // Latched "the dock should be up". Only evaluateAutohide() and the hide
    // timer touch it, so the reveal/hide decision lives in exactly one place.
    property bool dockHeld:       false
    property bool _dockAnimating: false
    // Keeps the icon strip rendered until the slide-out has actually finished.
    property bool contentRendered: true

    readonly property bool dockVisible: !config.autohide || dockHeld

    // ── Icon-bar hover zone ───────────────────────────────────────────────
    readonly property int stripThickness: config.iconSize + config.padding * 2
    // Slack above the strip covers the hover lift and the click-bounce
    // overshoot: an icon that pops up under the cursor must not read as the
    // cursor having left the bar.
    readonly property int zoneSlack: config.hoverLiftPx + 24

    // pos is in root-item coordinates, which match window coordinates.
    function pointerInDockZone(pos) {
        switch (config.position) {
        case "top":   return pos.y <= stripThickness + zoneSlack
        case "left":  return pos.x <= stripThickness + zoneSlack
        case "right": return pos.x >= width  - stripThickness - zoneSlack
        default:      return pos.y >= height - stripThickness - zoneSlack
        }
    }

    // Single source of truth for the auto-hide decision. Called on every
    // pointer, menu and config change — never assumes an event was reliable.
    function evaluateAutohide() {
        if (!config.autohide) {
            hideTimer.stop()
            return
        }
        if (pointerOnDock || menuOpen) {
            hideTimer.stop()
            root.dockHeld = true
            return
        }
        // Freshly revealed: let the reveal settle before any hide countdown
        // starts, so a hover event delivered while the dock is still sliding
        // in can't bounce it straight back out. revealDwell re-evaluates.
        if (revealDwell.running)
            return
        if (!hideTimer.running)
            hideTimer.start()
    }

    onPointerOnDockChanged: evaluateAutohide()
    onMenuOpenChanged:      evaluateAutohide()

    Connections {
        target: config
        function onConfigChanged() { root.evaluateAutohide() }
    }

    Component.onCompleted: {
        // Match the real Wayland surface to the initial visibility so a
        // dock that starts auto-hidden doesn't block the screen edge.
        if (typeof dockWindow !== "undefined")
            dockWindow.setRevealed(root.dockVisible)
    }

    onDockVisibleChanged: {
        console.log("[kdock autohide] dockVisible →", dockVisible,
                    "  autohide:", config.autohide, "  dockHeld:", dockHeld)
        if (dockVisible) {
            // Lock out hover-on-icon animations while the dock slides in
            root._dockAnimating   = true
            root.contentRendered  = true
            animDoneTimer.restart()
            revealDwell.restart()
            maskHideTimer.stop()
            // Widen the input region back to the whole surface immediately so
            // the icons are clickable as soon as they're on screen.
            if (typeof dockWindow !== "undefined") dockWindow.setRevealed(true)
        } else {
            hideTimer.stop()
            // Wait for the slide-out to finish before narrowing the input
            // region back to the reveal strip — doing it early would make the
            // still-visible icons unclickable.
            maskHideTimer.restart()
        }
    }

    // Delay between the cursor leaving the icon bar and the dock sliding away.
    // Long by design (2.5 s default): a dock that vanishes the instant the
    // cursor clips its edge is impossible to aim at.
    Timer {
        id: hideTimer
        interval: Math.max(250, config.autohideDelayMs)
        repeat:   false
        onTriggered: {
            // Re-check: the cursor may have come back, or a menu may have
            // opened, at any point while the countdown was running.
            if (root.pointerOnDock || root.menuOpen || !config.autohide) {
                root.evaluateAutohide()
                return
            }
            console.log("[kdock autohide] hide delay elapsed → hiding dock")
            root.dockHeld = false
        }
    }

    // Minimum time the dock stays up after revealing, before a hide countdown
    // may start. Covers the 220 ms slide-in plus settle.
    Timer {
        id: revealDwell
        interval: 700
        repeat:   false
        onTriggered: root.evaluateAutohide()
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

    // Narrow the input region back to the reveal strip, and stop rendering
    // the strip, once the 220 ms slide-out transform has finished.
    Timer {
        id: maskHideTimer
        interval: 240
        repeat:   false
        onTriggered: {
            if (typeof dockWindow !== "undefined") dockWindow.setRevealed(false)
            root.contentRendered = false
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
            root.pointerInWindow = hovered
            if (hovered) {
                // point.position is already up to date on the enter event.
                root.pointerInZone = root.pointerInDockZone(point.position)
                console.log("[kdock autohide] window ENTERED at", point.position,
                            " inZone:", root.pointerInZone)
            } else {
                console.log("[kdock autohide] window EXITED")
            }
        }

        // Tracks the cursor across the window so leaving the icon bar for the
        // transparent overflow above it is noticed even without an exit event.
        onPointChanged: {
            if (hovered)
                root.pointerInZone = root.pointerInDockZone(point.position)
        }
    }

    // ── Dock surface ─────────────────────────────────────────────────────
    DockBar {
        id: dockBar
        anchors.fill: parent
        visible:       root.contentRendered
        position:      config.position
        dockVisible:   root.dockVisible
        dockAnimating: root._dockAnimating

        transform: Translate {
            // Slide by the VISUAL dock thickness (icon strip height).
            // Window is taller than the strip to accommodate hover-lift overflow.
            readonly property int baseThickness: root.stripThickness
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
