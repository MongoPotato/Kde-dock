// SettingsPanel.qml — the user-facing settings UI.
// Opened by ContextMenu.qml when the user picks "Dock settings…".
// All values are read from the "config" context property (ConfigWatcher)
// and written via the "settings" context property (SettingsController).
//
// Design rule: every row must be understandable without reading a manual.
// Labels use sentence case. Units are always shown next to numeric controls.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Popup {
    id: root

    // Position above the dock, centred on screen
    x: (parent ? parent.width  / 2 - width  / 2 : 0)
    y: (parent ? parent.height / 2 - height / 2 : 0)

    width:  460
    height: settingsColumn.implicitHeight + 40
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 20

    background: Rectangle {
        color: "#2a2a3e"
        radius: 12
        border.color: "#444466"
        border.width: 1
    }

    ColumnLayout {
        id: settingsColumn
        anchors.fill: parent
        spacing: 14

        // ── Title ─────────────────────────────────────────────────────────
        Text {
            text: "Dock settings"
            font.pixelSize: 18
            font.bold: true
            color: "white"
            Layout.alignment: Qt.AlignHCenter
        }

        Rectangle { height: 1; color: "#444466"; Layout.fillWidth: true }

        // ── Icon size ─────────────────────────────────────────────────────
        RowLayout {
            spacing: 12
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 2
                Text { text: "Icon size"; color: "white"; font.pixelSize: 13 }
                Text { text: "24 – 128 px"; color: "#888"; font.pixelSize: 11 }
            }

            Slider {
                id: iconSizeSlider
                Layout.fillWidth: true
                from: config.scrollMinSize
                to:   config.scrollMaxSize
                value: config.iconSize
                stepSize: 1
                onMoved: settings.applyIconSize(value)
            }

            Text {
                text: Math.round(iconSizeSlider.value) + " px"
                color: "white"
                font.pixelSize: 13
                horizontalAlignment: Text.AlignRight
                Layout.minimumWidth: 50
            }
        }

        // ── Magnify on hover ──────────────────────────────────────────────
        RowLayout {
            spacing: 12
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 2
                Text { text: "Magnify on hover"; color: "white"; font.pixelSize: 13 }
                Text { text: "1.0 – 3.0 ×"; color: "#888"; font.pixelSize: 11 }
            }

            Slider {
                id: magnifySlider
                Layout.fillWidth: true
                from: 1.0
                to:   3.0
                value: config.magnifyScale
                stepSize: 0.05
                onMoved: settings.applyMagnifyScale(value)
            }

            Text {
                text: magnifySlider.value.toFixed(2) + " ×"
                color: "white"
                font.pixelSize: 13
                horizontalAlignment: Text.AlignRight
                Layout.minimumWidth: 50
            }
        }

        // ── Lift on hover ─────────────────────────────────────────────────
        RowLayout {
            spacing: 12
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 2
                Text { text: "Lift on hover"; color: "white"; font.pixelSize: 13 }
                Text { text: "0 – 40 px"; color: "#888"; font.pixelSize: 11 }
            }

            Slider {
                id: liftSlider
                Layout.fillWidth: true
                from: 0
                to:   40
                value: config.hoverLiftPx
                stepSize: 1
                onMoved: settings.applyHoverLift(value)
            }

            Text {
                text: Math.round(liftSlider.value) + " px"
                color: "white"
                font.pixelSize: 13
                horizontalAlignment: Text.AlignRight
                Layout.minimumWidth: 50
            }
        }

        Rectangle { height: 1; color: "#444466"; Layout.fillWidth: true }

        // ── Icon background shape ─────────────────────────────────────────
        RowLayout {
            spacing: 12
            Layout.fillWidth: true

            Text {
                text: "Icon background"
                color: "white"
                font.pixelSize: 13
                Layout.fillWidth: true
            }

            ComboBox {
                id: iconBgCombo
                model: ["None", "Circle", "Pill", "Squircle"]
                currentIndex: {
                    switch (config.iconBgShape) {
                    case "circle":   return 1
                    case "pill":     return 2
                    case "squircle": return 3
                    default:         return 0
                    }
                }
                onActivated: {
                    const shapes = ["none", "circle", "pill", "squircle"]
                    settings.applyIconBgShape(shapes[currentIndex])
                }
            }
        }

        // ── Icon background opacity ───────────────────────────────────────
        RowLayout {
            spacing: 12
            Layout.fillWidth: true
            visible: config.iconBgShape !== "none"

            ColumnLayout {
                spacing: 2
                Text { text: "Background opacity"; color: "white"; font.pixelSize: 13 }
                Text { text: "0 – 100 %"; color: "#888"; font.pixelSize: 11 }
            }

            Slider {
                id: iconBgOpacitySlider
                Layout.fillWidth: true
                from: 0.0
                to:   1.0
                value: config.iconBgOpacity
                stepSize: 0.01
                onMoved: settings.applyIconBgOpacity(value)
            }

            Text {
                text: Math.round(iconBgOpacitySlider.value * 100) + " %"
                color: "white"
                font.pixelSize: 13
                horizontalAlignment: Text.AlignRight
                Layout.minimumWidth: 50
            }
        }

        // ── Dock opacity ──────────────────────────────────────────────────
        RowLayout {
            spacing: 12
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 2
                Text { text: "Dock opacity"; color: "white"; font.pixelSize: 13 }
                Text { text: "10 – 100 %"; color: "#888"; font.pixelSize: 11 }
            }

            Slider {
                id: dockOpacitySlider
                Layout.fillWidth: true
                from: 0.1
                to:   1.0
                value: config.backgroundOpacity
                stepSize: 0.01
                onMoved: settings.applyDockOpacity(value)
            }

            Text {
                text: Math.round(dockOpacitySlider.value * 100) + " %"
                color: "white"
                font.pixelSize: 13
                horizontalAlignment: Text.AlignRight
                Layout.minimumWidth: 50
            }
        }

        Rectangle { height: 1; color: "#444466"; Layout.fillWidth: true }

        // ── Auto-hide ─────────────────────────────────────────────────────
        RowLayout {
            spacing: 12
            Layout.fillWidth: true

            Text {
                text: "Auto-hide dock"
                color: "white"
                font.pixelSize: 13
                Layout.fillWidth: true
            }

            Switch {
                id: autohideSwitch
                checked: config.autohide
                onToggled: settings.applyAutohide(checked)
            }
        }

        Rectangle { height: 1; color: "#444466"; Layout.fillWidth: true }

        // ── Icon theme (read-only, from KDE) ──────────────────────────────
        RowLayout {
            spacing: 12
            Layout.fillWidth: true

            ColumnLayout {
                spacing: 2
                Text { text: "Icon theme"; color: "white"; font.pixelSize: 13 }
                Text {
                    text: "Detected from KDE global settings"
                    color: "#888"
                    font.pixelSize: 11
                }
            }

            Text {
                text: iconThemeDetector.currentTheme
                color: "#aaddff"
                font.pixelSize: 13
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
            }

            Button {
                text: "Reload"
                flat: true
                onClicked: iconThemeDetector.redetect()
            }
        }

        Rectangle { height: 1; color: "#444466"; Layout.fillWidth: true }

        // ── Bottom buttons ────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true

            Button {
                text: "Reset to defaults"
                flat: true
                onClicked: settings.resetDefaults()
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "Close"
                onClicked: root.close()
            }
        }
    }
}
