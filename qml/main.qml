// main.qml is the QML entry point loaded by QQmlApplicationEngine.
// It acts purely as a wiring layer between the C++ context properties
// and the visual DockBar component.
//
// Context properties available:
//   config              → ConfigWatcher*       (position, iconSize, autohide, ...)
//   settings            → SettingsController*  (validated mutators)
//   dockModel           → DockModel*           (list of dock entries)
//   taskTracker         → TaskTracker*         (running window state)
//   iconThemeDetector   → IconThemeDetector*   (KDE theme name)

import QtQuick 2.15
import QtQuick.Window 2.15

Window {
    id: root

    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus

    readonly property bool isHorizontal: config.position === "bottom"
                                      || config.position === "top"

    width:  isHorizontal ? Screen.width  : config.iconSize + config.padding * 2
    height: isHorizontal ? config.iconSize + config.padding * 2 : Screen.height

    visible: true

    // Auto-hide: reveal when mouse enters, hide when it leaves
    property bool dockHidden: config.autohide

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        propagateComposedEvents: true
        onEntered: if (config.autohide) root.dockHidden = false
        onExited:  if (config.autohide) root.dockHidden = true
    }

    DockBar {
        id: dockBar
        anchors.centerIn: parent
        position: config.position

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
            x: (config.position === "left" || config.position === "right")
               ? (root.dockHidden ? dockBar.hiddenOffset : 0) : 0
            y: (config.position === "bottom" || config.position === "top")
               ? (root.dockHidden ? dockBar.hiddenOffset : 0) : 0

            Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
        }
    }

    // Settings panel — loaded on demand and shared between ContextMenu instances
    SettingsPanel {
        id: settingsPanel
        parent: root.contentItem
    }
}
