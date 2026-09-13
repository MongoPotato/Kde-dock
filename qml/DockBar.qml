// DockBar is the visible dock surface.
//
// Adaptive colour: when config.adaptiveColor is on, the background tints
// toward the dominant colour of the active window's icon.

import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root

    property string position: "bottom"
    property bool dockVisible:   true
    property bool dockAnimating: false   // true while the slide-in animation runs
    readonly property bool isHorizontal: position === "bottom" || position === "top"

    // Number of context menus currently open anywhere in the dock — the bar's
    // own and every DockItem's. main.qml keeps an auto-hiding dock revealed
    // while this is non-zero: the cursor is over the menu window rather than
    // the dock, but the user is obviously still working with the dock.
    property int openMenuCount: 0

    onDockVisibleChanged: console.log("[kdock autohide] DockBar.dockVisible →", dockVisible)

    implicitWidth:  isHorizontal ? itemRow.implicitWidth     + config.padding * 2
                                 : config.dockVisualThickness
    implicitHeight: isHorizontal ? config.dockVisualThickness
                                 : itemColumn.implicitHeight + config.padding * 2

    // ── Background ───────────────────────────────────────────────────────
    // Anchored to the dock EDGE. The window may be taller than the strip
    // (to allow hover-lift overflow) so the background only fills the
    // visual strip, not the entire window.
    //
    // stripSize comes from config.dockVisualThickness, the same number the
    // compositor is asked to reserve. Computing it here as
    // iconSize + padding * 2 made the background SHORTER than the icon row it
    // contains — icon pills and glow rings poked out of the top of the bar.
    Rectangle {
        readonly property int stripSize: config.dockVisualThickness

        anchors.left:   (isHorizontal || position === "left")  ? parent.left  : undefined
        anchors.right:  (isHorizontal || position === "right") ? parent.right : undefined
        anchors.top:    (position === "top"    || !isHorizontal) ? parent.top    : undefined
        anchors.bottom: (position === "bottom" || !isHorizontal) ? parent.bottom : undefined

        width:  isHorizontal ? parent.width : stripSize
        height: isHorizontal ? stripSize    : parent.height

        radius: config.backgroundRadius

        // Blend base background colour with adaptive tint when enabled
        color: {
            if (!config.adaptiveColor || taskTracker.activeAppId === "")
                return config.backgroundColor
            const tint = dockModel.iconDominantColor(
                dockModel.iconNameForApp(taskTracker.activeAppId))
            return Qt.rgba(
                tint.r * 0.25 + Qt.color(config.backgroundColor).r * 0.75,
                tint.g * 0.25 + Qt.color(config.backgroundColor).g * 0.75,
                tint.b * 0.25 + Qt.color(config.backgroundColor).b * 0.75,
                1.0)
        }
        opacity: config.backgroundOpacity

        Behavior on color   { ColorAnimation  { duration: 400 } }
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    // ── Right-click → dock menu ───────────────────────────────────────────
    // A TapHandler rather than a MouseArea. The MouseArea this replaces had to
    // win an exclusive grab over the whole bar to see a click, so it competed
    // with everything else on the dock and right-clicks on the free area were
    // easy to lose. A handler is offered the event on its own and fires on
    // release-within-bounds, so the menu comes up wherever the bar itself is
    // under the cursor. Icons still answer their own right-click first (their
    // MouseArea is in front), which is the more specific menu of the two.
    TapHandler {
        id: barRightClick
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds

        onSingleTapped: (eventPoint, button) => {
            // dockWindow.mapToScreen, not mapToGlobal: Qt has no idea where a
            // layer surface actually sits, so mapToGlobal returns coordinates
            // relative to the dock and the menu landed nowhere near the click.
            const sp = dockWindow.mapToScreen(eventPoint.position.x,
                                              eventPoint.position.y)
            barContextMenu.mode = "dock"
            barContextMenu.openAt(sp.x, sp.y)
        }
    }

    // ── Scroll-to-zoom ───────────────────────────────────────────────────
    WheelHandler {
        id: scrollZoom
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => {
            if (event.angleDelta.y === 0) { event.accepted = false; return }
            const delta = event.angleDelta.y / 120
            const newSize = Math.max(config.scrollMinSize,
                            Math.min(config.scrollMaxSize,
                                     config.iconSize + delta * config.scrollStepPx))
            config.setIconSize(Math.round(newSize))
            event.accepted = true
        }
    }

    // ── Icon layout ───────────────────────────────────────────────────────
    Row {
        id: itemRow
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: position === "bottom" ? parent.bottom : undefined
        anchors.top:    position === "top"    ? parent.top    : undefined
        anchors.bottomMargin: position === "bottom" ? config.padding : 0
        anchors.topMargin:    position === "top"    ? config.padding : 0
        visible: isHorizontal
        spacing: config.spacing

        Repeater {
            model: isHorizontal ? dockModel : null
            delegate: DockItem {
                dockBar:       root
                position:      root.position
                dockAnimating: root.dockAnimating
            }
        }
    }

    Column {
        id: itemColumn
        anchors.verticalCenter: parent.verticalCenter
        anchors.left:  position === "left"  ? parent.left  : undefined
        anchors.right: position === "right" ? parent.right : undefined
        anchors.leftMargin:  position === "left"  ? config.padding : 0
        anchors.rightMargin: position === "right" ? config.padding : 0
        visible: !isHorizontal
        spacing: config.spacing

        Repeater {
            model: isHorizontal ? null : dockModel
            delegate: DockItem {
                dockBar:       root
                position:      root.position
                dockAnimating: root.dockAnimating
            }
        }
    }

    // ── Context menu ──────────────────────────────────────────────────────
    ContextMenu {
        id: barContextMenu
        onVisibleChanged: root.openMenuCount += visible ? 1 : -1
    }
}
