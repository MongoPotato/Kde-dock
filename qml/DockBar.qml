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

    // The dock's menu and tooltip are SHARED, not one pair per icon.
    //
    // Every DockItem used to own a ContextMenu and a Tooltip, and both are
    // real windows — with eight icons that was eighteen windows alive at all
    // times, each with its own scene graph, for two that can ever be on
    // screen at once. Only one menu can be open and only one tooltip shown,
    // so one of each lives here and the items drive them.
    readonly property int openMenuCount:
        (menuLoader.item && menuLoader.item.visible) ? 1 : 0

    // ── Shared popups ─────────────────────────────────────────────────────
    // Built on first use rather than at startup — a dock that is never
    // right-clicked never pays for a menu window.
    function openDockMenu(screenX, screenY) {
        menuLoader.active = true
        menuLoader.item.appId = ""
        menuLoader.item.mode  = "dock"
        menuLoader.item.openAt(screenX, screenY)
    }

    function openAppMenu(appId, screenX, screenY) {
        menuLoader.active = true
        menuLoader.item.appId = appId
        menuLoader.item.mode  = "app"
        menuLoader.item.openAt(screenX, screenY)
    }

    function showTooltip(item, text) {
        tooltipLoader.active = true
        tooltipLoader.item.parentItem = item
        tooltipLoader.item.text = text
        tooltipLoader.item.show()
    }

    // Only the item that put the tooltip up may take it down: the cursor
    // moving between neighbouring icons produces the new item's show() before
    // the old item's hide(), which would otherwise cancel it immediately.
    function hideTooltip(item) {
        if (tooltipLoader.item && tooltipLoader.item.parentItem === item)
            tooltipLoader.item.hide()
    }

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
            root.openDockMenu(sp.x, sp.y)
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

    // ── Shared menu and tooltip (see openMenuCount above) ─────────────────
    Loader {
        id: menuLoader
        active: false
        sourceComponent: ContextMenu {}
    }

    Loader {
        id: tooltipLoader
        active: false
        sourceComponent: Tooltip { dockPosition: root.position }
    }
}
