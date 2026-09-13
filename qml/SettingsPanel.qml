// SettingsPanel — visual appearance settings.
// Implemented as a floating Window (not a Popup) so it appears outside the
// thin dock strip and is never clipped.  Drag the title bar to reposition.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
    id: root

    title: "Dock settings"
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"

    width:  480
    // Cap height at 85 % of primary screen so the panel is never taller than
    // the display.  Content scrolls when it overflows.
    height: Math.min(Math.round(Qt.application.screens[0].height * 0.85),
                     settingsCol.implicitHeight + titleBar.height + 32)

    // API-compatible shims so main.qml's Connections still work
    function open() {
        const scr = Qt.application.screens[0]
        x = scr.virtualX + Math.round((scr.width  - width)  / 2)
        y = scr.virtualY + Math.round((scr.height - height) / 2)
        visible = true
        raise()
        requestActivate()
    }
    function close() { visible = false }

    Shortcut { context: Qt.WindowShortcut; sequence: "Escape"; onActivated: root.close() }

    // ── Window chrome ─────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: "#242438"
        radius: 12
        border.color: "#44446a"
        border.width: 1

        // ── Title bar (drag handle) ───────────────────────────────────────
        Rectangle {
            id: titleBar
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 46
            color: "#1e1e30"
            // Square bottom corners to join the body seamlessly
            radius: 0
            topLeftRadius:  12
            topRightRadius: 12

            Text {
                anchors.centerIn: parent
                text: "Dock settings"
                color: "white"
                font.pixelSize: 15
                font.bold: true
            }

            // Close button (×)
            Text {
                id: closeBtn
                anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                text: "✕"
                color: closeMa.containsMouse ? "white" : "#888"
                font.pixelSize: 15
                MouseArea {
                    id: closeMa
                    anchors { fill: parent; margins: -6 }
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.close()
                }
            }

            // Drag-to-move — occupies the title bar except the close button
            MouseArea {
                anchors { fill: parent; rightMargin: 40 }
                property point dragStart
                cursorShape: Qt.SizeAllCursor
                onPressed:  (m) => { dragStart = Qt.point(m.x, m.y) }
                onPositionChanged: (m) => {
                    if (pressed) { root.x += m.x - dragStart.x; root.y += m.y - dragStart.y }
                }
            }
        }

        // ── Scrollable settings content ───────────────────────────────────
        ScrollView {
            anchors {
                top: titleBar.bottom; left: parent.left; right: parent.right; bottom: parent.bottom
                margins: 16; topMargin: 10
            }
            contentHeight: settingsCol.implicitHeight
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                id: settingsCol
                width: parent.width - 4   // leave room for scrollbar
                spacing: 14

                // ── Icon size ─────────────────────────────────────────────
                RowLayout { spacing: 12; Layout.fillWidth: true
                    ColumnLayout { spacing: 2
                        Text { text: "Icon size";    color: "white"; font.pixelSize: 13 }
                        Text { text: "24 – 128 px";  color: "#888";  font.pixelSize: 11 }
                    }
                    Slider {
                        id: iconSizeSlider
                        Layout.fillWidth: true
                        from: config.scrollMinSize; to: config.scrollMaxSize
                        value: config.iconSize; stepSize: 1
                        onMoved: settings.applyIconSize(value)
                    }
                    Text { text: Math.round(iconSizeSlider.value) + " px"; color: "white"; font.pixelSize: 13; Layout.minimumWidth: 52 }
                }

                // ── Magnify on hover ──────────────────────────────────────
                RowLayout { spacing: 12; Layout.fillWidth: true
                    ColumnLayout { spacing: 2
                        Text { text: "Magnify on hover"; color: "white"; font.pixelSize: 13 }
                        Text { text: "1.0 = off, up to 3.0 ×"; color: "#888"; font.pixelSize: 11 }
                    }
                    Slider {
                        id: magnifySlider
                        Layout.fillWidth: true
                        from: 1.0; to: 3.0; value: config.magnifyScale; stepSize: 0.05
                        onMoved: settings.applyMagnifyScale(value)
                    }
                    Text { text: magnifySlider.value.toFixed(2) + " ×"; color: "white"; font.pixelSize: 13; Layout.minimumWidth: 52 }
                }

                // ── Lift on hover ─────────────────────────────────────────
                RowLayout { spacing: 12; Layout.fillWidth: true
                    ColumnLayout { spacing: 2
                        Text { text: "Lift on hover"; color: "white"; font.pixelSize: 13 }
                        Text { text: "0 – 40 px";    color: "#888";  font.pixelSize: 11 }
                    }
                    Slider {
                        id: liftSlider
                        Layout.fillWidth: true
                        from: 0; to: 40; value: config.hoverLiftPx; stepSize: 1
                        onMoved: settings.applyHoverLift(value)
                    }
                    Text { text: Math.round(liftSlider.value) + " px"; color: "white"; font.pixelSize: 13; Layout.minimumWidth: 52 }
                }

                Rectangle { height: 1; color: "#444466"; Layout.fillWidth: true }

                // ── Dock position ─────────────────────────────────────────
                RowLayout { spacing: 12; Layout.fillWidth: true
                    Text { text: "Dock position"; color: "white"; font.pixelSize: 13; Layout.fillWidth: true }
                    ComboBox {
                        model: ["Bottom", "Top", "Left", "Right"]
                        currentIndex: { switch (config.position) { case "top": return 1; case "left": return 2; case "right": return 3; default: return 0 } }
                        background: Rectangle { color: "#3a3a5e"; radius: 4; border.color: "#555577"; border.width: 1 }
                        contentItem: Text { text: parent.displayText; color: "white"; font.pixelSize: 13; leftPadding: 8; verticalAlignment: Text.AlignVCenter }
                        onActivated: {
                            const pos = ["bottom", "top", "left", "right"]
                            config.setPosition(pos[currentIndex])
                        }
                    }
                }

                // ── Dock opacity ──────────────────────────────────────────
                RowLayout { spacing: 12; Layout.fillWidth: true
                    ColumnLayout { spacing: 2
                        Text { text: "Dock opacity";            color: "white"; font.pixelSize: 13 }
                        Text { text: "0 = fully transparent";  color: "#888";  font.pixelSize: 11 }
                    }
                    Slider {
                        id: dockOpacitySlider
                        Layout.fillWidth: true
                        from: 0.0; to: 1.0; value: config.backgroundOpacity; stepSize: 0.01
                        onMoved: settings.applyDockOpacity(value)
                    }
                    Text { text: Math.round(dockOpacitySlider.value * 100) + " %"; color: "white"; font.pixelSize: 13; Layout.minimumWidth: 52 }
                }

                Rectangle { height: 1; color: "#444466"; Layout.fillWidth: true }

                // ── Icon background shape ─────────────────────────────────
                RowLayout { spacing: 12; Layout.fillWidth: true
                    Text { text: "Icon background"; color: "white"; font.pixelSize: 13; Layout.fillWidth: true }
                    ComboBox {
                        model: ["None", "Circle", "Pill", "Squircle"]
                        currentIndex: { switch (config.iconBgShape) { case "circle": return 1; case "pill": return 2; case "squircle": return 3; default: return 0 } }
                        background: Rectangle { color: "#3a3a5e"; radius: 4; border.color: "#555577"; border.width: 1 }
                        contentItem: Text { text: parent.displayText; color: "white"; font.pixelSize: 13; leftPadding: 8; verticalAlignment: Text.AlignVCenter }
                        onActivated: {
                            const shapes = ["none", "circle", "pill", "squircle"]
                            settings.applyIconBgShape(shapes[currentIndex])
                        }
                    }
                }

                Rectangle { height: 1; color: "#444466"; Layout.fillWidth: true }

                // ── Blur background ───────────────────────────────────────
                RowLayout { spacing: 12; Layout.fillWidth: true
                    ColumnLayout { spacing: 2
                        Text { text: "Blur background";       color: "white"; font.pixelSize: 13 }
                        Text { text: "Requires KDE compositor"; color: "#888"; font.pixelSize: 11 }
                    }
                    Item { Layout.fillWidth: true }
                    Switch {
                        checked: config.blurEnabled
                        onToggled: config.setBlurEnabled(checked)
                    }
                }

                // ── Adaptive colour tint ──────────────────────────────────
                RowLayout { spacing: 12; Layout.fillWidth: true
                    ColumnLayout { spacing: 2
                        Text { text: "Adaptive colour tint";       color: "white"; font.pixelSize: 13 }
                        Text { text: "Tints dock from active app"; color: "#888";  font.pixelSize: 11 }
                    }
                    Item { Layout.fillWidth: true }
                    Switch {
                        checked: config.adaptiveColor
                        onToggled: config.setAdaptiveColor(checked)
                    }
                }

                // ── Reserve screen space ──────────────────────────────────
                RowLayout { spacing: 12; Layout.fillWidth: true
                    enabled: config.autohideMode === "never"
                    opacity: enabled ? 1.0 : 0.45
                    ColumnLayout { spacing: 2
                        Text { text: "Reserve screen space"; color: "white"; font.pixelSize: 13 }
                        Text {
                            text: config.autohideMode !== "never"
                                  ? "Not reserved unless the dock is always visible"
                                  : "Keeps windows out of the dock's "
                                    + config.dockReservedThickness + " px strip"
                            color: "#888"; font.pixelSize: 11
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Switch {
                        checked: config.reserveSpace
                        onToggled: settings.applyReserveSpace(checked)
                    }
                }

                // ── Dock visibility ───────────────────────────────────────
                RowLayout { spacing: 12; Layout.fillWidth: true
                    ColumnLayout { spacing: 2
                        Text { text: "Dock visibility"; color: "white"; font.pixelSize: 13 }
                        Text {
                            text: {
                                switch (config.autohideMode) {
                                case "always": return "Hidden until the cursor reaches the edge"
                                case "dodge":  return "Hides only while a window covers the dock"
                                default:       return "Always on screen"
                                }
                            }
                            color: "#888"; font.pixelSize: 11
                        }
                    }
                    Item { Layout.fillWidth: true }
                    ComboBox {
                        id: visibilityMode
                        Layout.minimumWidth: 190
                        model: ["Always visible", "Hide when in the way", "Always auto-hide"]
                        readonly property var modeKeys: ["never", "dodge", "always"]
                        currentIndex: modeKeys.indexOf(config.autohideMode) >= 0
                                      ? modeKeys.indexOf(config.autohideMode) : 0
                        background: Rectangle { color: "#3a3a5e"; radius: 4; border.color: "#555577"; border.width: 1 }
                        contentItem: Text {
                            text: parent.displayText; color: "white"; font.pixelSize: 13
                            leftPadding: 8; verticalAlignment: Text.AlignVCenter
                        }
                        onActivated: settings.applyAutohideMode(modeKeys[currentIndex])
                    }
                }

                // ── Auto-hide delay ───────────────────────────────────────
                RowLayout { spacing: 12; Layout.fillWidth: true
                    enabled: config.autohideMode !== "never"
                    opacity: enabled ? 1.0 : 0.45
                    ColumnLayout { spacing: 2
                        Text { text: "Auto-hide delay"; color: "white"; font.pixelSize: 13 }
                        Text { text: "Wait before hiding, 0.25 – 10 s"; color: "#888"; font.pixelSize: 11 }
                    }
                    Slider {
                        id: hideDelaySlider
                        Layout.fillWidth: true
                        from: 250; to: 10000; value: config.autohideDelayMs; stepSize: 250
                        onMoved: settings.applyAutohideDelay(value)
                    }
                    Text {
                        text: (hideDelaySlider.value / 1000).toFixed(2) + " s"
                        color: "white"; font.pixelSize: 13; Layout.minimumWidth: 52
                    }
                }

                Rectangle { height: 1; color: "#444466"; Layout.fillWidth: true }

                // ── Icon theme ────────────────────────────────────────────
                RowLayout { spacing: 12; Layout.fillWidth: true
                    ColumnLayout { spacing: 2
                        Text { text: "Icon theme";                        color: "white";   font.pixelSize: 13 }
                        Text { text: iconThemeDetector.currentTheme;      color: "#aaddff"; font.pixelSize: 12 }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "Reload"
                        flat: true
                        palette.buttonText: "#aaddff"
                        onClicked: iconThemeDetector.redetect()
                    }
                }

                Rectangle { height: 1; color: "#444466"; Layout.fillWidth: true }

                // ── Config file / JSON viewer ─────────────────────────────
                property bool showConfigJson: false

                RowLayout { spacing: 12; Layout.fillWidth: true
                    ColumnLayout { spacing: 2
                        Text { text: "Config file";    color: "white";   font.pixelSize: 13 }
                        Text {
                            text: config.configFilePath
                            color: "#aaddff"; font.pixelSize: 10
                            elide: Text.ElideMiddle
                            Layout.maximumWidth: 320
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: settingsCol.showConfigJson ? "Hide JSON" : "View JSON"
                        flat: true
                        palette.buttonText: "#aaddff"
                        onClicked: settingsCol.showConfigJson = !settingsCol.showConfigJson
                    }
                }

                ScrollView {
                    visible: settingsCol.showConfigJson
                    Layout.fillWidth: true
                    implicitHeight: 200
                    clip: true

                    TextArea {
                        text: config.configJson
                        readOnly: true
                        color: "#ccddff"
                        font.family: "monospace"
                        font.pixelSize: 11
                        wrapMode: TextArea.Wrap
                        background: Rectangle { color: "#111122"; radius: 4 }
                        selectByMouse: true
                        padding: 8
                    }
                }

                Rectangle { height: 1; color: "#444466"; Layout.fillWidth: true }

                // ── Bottom action row ─────────────────────────────────────
                RowLayout { Layout.fillWidth: true
                    Button {
                        text: "Manage dock apps…"
                        flat: true
                        palette.buttonText: "#aabbff"
                        onClicked: { root.close(); settings.requestManageApps() }
                    }
                    Button {
                        text: "Reset to defaults"
                        flat: true
                        palette.buttonText: "#ffaaaa"
                        onClicked: settings.resetDefaults()
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "Close"
                        onClicked: root.close()
                    }
                }

                // Bottom spacer so the last row has breathing room above the
                // window edge even when the scroll view is not active.
                Item { height: 4 }
            }
        }
    }
}
