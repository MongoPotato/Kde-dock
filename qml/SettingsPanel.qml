// SettingsPanel — visual appearance settings popup.
// App management is in AppPickerPanel (opened via "Manage dock apps…").

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Popup {
    id: root

    x: (parent ? parent.width  / 2 - width  / 2 : 0)
    y: (parent ? parent.height / 2 - height / 2 : 0)

    width: 460
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

        // Title
        Text {
            text: "Dock appearance"
            font.pixelSize: 18
            font.bold: true
            color: "white"
            Layout.alignment: Qt.AlignHCenter
        }

        Rectangle { height: 1; color: "#444466"; Layout.fillWidth: true }

        // Icon size
        RowLayout { spacing: 12; Layout.fillWidth: true
            ColumnLayout { spacing: 2
                Text { text: "Icon size"; color: "white"; font.pixelSize: 13 }
                Text { text: "24 – 128 px"; color: "#888"; font.pixelSize: 11 }
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

        // Magnify on hover
        RowLayout { spacing: 12; Layout.fillWidth: true
            ColumnLayout { spacing: 2
                Text { text: "Magnify on hover"; color: "white"; font.pixelSize: 13 }
                Text { text: "1.0 – 3.0 ×"; color: "#888"; font.pixelSize: 11 }
            }
            Slider {
                id: magnifySlider
                Layout.fillWidth: true
                from: 1.0; to: 3.0; value: config.magnifyScale; stepSize: 0.05
                onMoved: settings.applyMagnifyScale(value)
            }
            Text { text: magnifySlider.value.toFixed(2) + " ×"; color: "white"; font.pixelSize: 13; Layout.minimumWidth: 52 }
        }

        // Lift on hover
        RowLayout { spacing: 12; Layout.fillWidth: true
            ColumnLayout { spacing: 2
                Text { text: "Lift on hover"; color: "white"; font.pixelSize: 13 }
                Text { text: "0 – 40 px"; color: "#888"; font.pixelSize: 11 }
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

        // Dock position
        RowLayout { spacing: 12; Layout.fillWidth: true
            Text { text: "Dock position"; color: "white"; font.pixelSize: 13; Layout.fillWidth: true }
            ComboBox {
                model: ["Bottom", "Top", "Left", "Right"]
                currentIndex: { switch (config.position) { case "top": return 1; case "left": return 2; case "right": return 3; default: return 0 } }
                background: Rectangle { color: "#3a3a5e"; radius: 4; border.color: "#555577"; border.width: 1 }
                contentItem: Text { text: parent.displayText; color: "white"; font.pixelSize: 13; leftPadding: 8; verticalAlignment: Text.AlignVCenter }
                onActivated: {
                    const positions = ["bottom", "top", "left", "right"]
                    config.setPosition(positions[currentIndex])
                }
            }
        }

        // Dock opacity
        RowLayout { spacing: 12; Layout.fillWidth: true
            ColumnLayout { spacing: 2
                Text { text: "Dock opacity"; color: "white"; font.pixelSize: 13 }
                Text { text: "10 – 100 %"; color: "#888"; font.pixelSize: 11 }
            }
            Slider {
                id: dockOpacitySlider
                Layout.fillWidth: true
                from: 0.1; to: 1.0; value: config.backgroundOpacity; stepSize: 0.01
                onMoved: settings.applyDockOpacity(value)
            }
            Text { text: Math.round(dockOpacitySlider.value * 100) + " %"; color: "white"; font.pixelSize: 13; Layout.minimumWidth: 52 }
        }

        Rectangle { height: 1; color: "#444466"; Layout.fillWidth: true }

        // Icon background shape
        RowLayout { spacing: 12; Layout.fillWidth: true
            Text { text: "Icon background"; color: "white"; font.pixelSize: 13; Layout.fillWidth: true }
            ComboBox {
                id: iconBgCombo
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

        // Blur background
        RowLayout { spacing: 12; Layout.fillWidth: true
            ColumnLayout { spacing: 2
                Text { text: "Blur background"; color: "white"; font.pixelSize: 13 }
                Text { text: "Requires KDE compositor"; color: "#888"; font.pixelSize: 11 }
            }
            Item { Layout.fillWidth: true }
            Switch {
                checked: config.blurEnabled
                onToggled: config.setBlurEnabled(checked)
            }
        }

        // Adaptive color
        RowLayout { spacing: 12; Layout.fillWidth: true
            ColumnLayout { spacing: 2
                Text { text: "Adaptive colour tint"; color: "white"; font.pixelSize: 13 }
                Text { text: "Tints background from active app"; color: "#888"; font.pixelSize: 11 }
            }
            Item { Layout.fillWidth: true }
            Switch {
                checked: config.adaptiveColor
                onToggled: config.setAdaptiveColor(checked)
            }
        }

        // Auto-hide
        RowLayout { spacing: 12; Layout.fillWidth: true
            Text { text: "Auto-hide dock"; color: "white"; font.pixelSize: 13; Layout.fillWidth: true }
            Switch {
                checked: config.autohide
                onToggled: settings.applyAutohide(checked)
            }
        }

        Rectangle { height: 1; color: "#444466"; Layout.fillWidth: true }

        // Icon theme
        RowLayout { spacing: 12; Layout.fillWidth: true
            ColumnLayout { spacing: 2
                Text { text: "Icon theme"; color: "white"; font.pixelSize: 13 }
                Text { text: iconThemeDetector.currentTheme; color: "#aaddff"; font.pixelSize: 12 }
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

        // Bottom buttons
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
    }
}
