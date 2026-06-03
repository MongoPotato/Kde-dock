// main.qml is the QML entry point loaded by QQmlApplicationEngine.
// It acts purely as a wiring layer between the C++ context properties
// and the visual DockBar component.
//
// Context properties available:
//   config       → ConfigWatcher*  (position, iconSize, autohide, ...)
//   dockModel    → DockModel*      (list of dock entries)
//   taskTracker  → TaskTracker*    (running window state)

import QtQuick 2.15
import QtQuick.Window 2.15

Window {
    id: root

    // Make window transparent so the rounded dock bar shows through
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus

    // Adjust window dimensions based on dock orientation
    readonly property bool isHorizontal: config.position === "bottom"
                                      || config.position === "top"

    width:  isHorizontal ? Screen.width  : config.iconSize + config.padding * 2
    height: isHorizontal ? config.iconSize + config.padding * 2 : Screen.height

    visible: true

    // Auto-hide state
    property bool docHidden: config.autohide

    // Edge-reveal strip for auto-hide mode
    MouseArea {
        id: edgeStrip
        anchors.fill: parent
        hoverEnabled: true
        propagateComposedEvents: true

        onEntered: {
            if (config.autohide)
                revealAnimation.start()
        }
        onExited: {
            if (config.autohide)
                hideAnimation.start()
        }
    }

    DockBar {
        id: dockBar
        anchors.centerIn: parent
        position: config.position

        // Slide off-screen when hidden
        property int hiddenOffset: {
            switch (config.position) {
            case "bottom": return root.height
            case "top":    return -root.height
            case "left":   return -root.width
            case "right":  return root.width
            default:       return root.height
            }
        }

        transform: Translate {
            id: slideTranslate
            x: (config.position === "left" || config.position === "right")
               ? (root.docHidden ? dockBar.hiddenOffset : 0)
               : 0
            y: (config.position === "bottom" || config.position === "top")
               ? (root.docHidden ? dockBar.hiddenOffset : 0)
               : 0

            Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
        }
    }

    NumberAnimation {
        id: revealAnimation
        target: root
        property: "docHidden"
        to: 0
        duration: 0
        onStarted: root.docHidden = false
    }

    NumberAnimation {
        id: hideAnimation
        target: root
        property: "docHidden"
        to: 1
        duration: 0
        onStarted: root.docHidden = true
    }
}
