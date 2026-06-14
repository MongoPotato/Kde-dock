// AppPickerPanel — Dock Manager window.
//
// Opens as a regular OS window (not a popup) so it can be large enough
// to be useful.  All changes take effect immediately on the live dock
// via config.setPinnedApps()/config.save() — there is no separate
// Apply or Save step for icon changes.
//
// The preset system lets you name and save the current app list so you
// can switch between configurations (e.g. "Work", "Gaming").
//
// Context properties used:  config, dockModel, appLibrary, settings

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQml.Models 2.15

Window {
    id: root

    title: "Dock Manager"
    width: 780
    height: 540
    minimumWidth: 600
    minimumHeight: 420
    color: "#1a1a2e"
    flags: Qt.Dialog | Qt.WindowCloseButtonHint | Qt.WindowTitleHint | Qt.WindowStaysOnTopHint

    // ── Local list model mirrors config.pinnedApps ───────────────────────
    // Changes are applied to config immediately (no deferred save needed).
    ListModel { id: pinnedModel }

    function syncFromConfig() {
        pinnedModel.clear()
        const apps = config.pinnedApps
        for (let i = 0; i < apps.length; i++) {
            const id = apps[i]
            pinnedModel.append({
                appId:       id,
                displayName: dockModel.displayNameForApp(id),
                iconName:    dockModel.iconNameForApp(id),
            })
        }
    }

    function flushToConfig() {
        const ids = []
        for (let i = 0; i < pinnedModel.count; i++)
            ids.push(pinnedModel.get(i).appId)
        config.setPinnedApps(ids)
        config.save()
    }

    onVisibleChanged: if (visible) { syncFromConfig(); appLibrary.filter = "" }

    Connections {
        target: config
        function onConfigChanged() {
            // Refresh the local model when a preset is loaded from elsewhere
            if (!root.visible) return
            syncFromConfig()
        }
    }

    // ── Preset create dialog ─────────────────────────────────────────────
    Dialog {
        id: newPresetDialog
        title: "Save as preset"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Save | Dialog.Cancel

        background: Rectangle { color: "#2a2a3e"; radius: 8; border.color: "#555577"; border.width: 1 }

        contentItem: ColumnLayout {
            spacing: 10
            Label {
                text: "Enter a name for this preset:"
                color: "white"
            }
            TextField {
                id: presetNameField
                Layout.fillWidth: true
                placeholderText: "e.g. Work, Gaming…"
                background: Rectangle { color: "#3a3a5e"; radius: 4 }
                color: "white"
                Keys.onReturnPressed: newPresetDialog.accept()
            }
        }

        onAccepted: {
            const name = presetNameField.text.trim()
            if (name.length > 0) {
                flushToConfig()
                config.savePreset(name)
                presetCombo.updateModel()
            }
            presetNameField.clear()
        }
        onRejected: presetNameField.clear()
    }

    // ── Header bar ───────────────────────────────────────────────────────
    Rectangle {
        id: header
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 52
        color: "#12122a"

        RowLayout {
            anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
            spacing: 12

            Text {
                text: "Dock Manager"
                font.pixelSize: 16
                font.bold: true
                color: "white"
            }

            Item { Layout.fillWidth: true }

            Text { text: "Preset:"; color: "#aaa"; font.pixelSize: 13 }

            ComboBox {
                id: presetCombo
                implicitWidth: 140
                model: ListModel { id: presetListModel }

                background: Rectangle { color: "#2a2a4a"; radius: 4; border.color: "#555577"; border.width: 1 }
                contentItem: Text { text: presetCombo.displayText; color: "white"; font.pixelSize: 13; leftPadding: 8; verticalAlignment: Text.AlignVCenter }

                delegate: ItemDelegate {
                    width: presetCombo.width
                    contentItem: Text { text: model.name; color: "white"; font.pixelSize: 13 }
                    background: Rectangle { color: highlighted ? "#3a3a6a" : "#1a1a3a" }
                    highlighted: presetCombo.highlightedIndex === index
                }

                function updateModel() {
                    presetListModel.clear()
                    const names = config.presetNames()
                    for (let i = 0; i < names.length; i++)
                        presetListModel.append({ name: names[i] })
                    if (names.length === 0)
                        presetListModel.append({ name: "Default" })
                }

                onActivated: {
                    const name = presetListModel.get(currentIndex).name
                    if (name !== "Default" || config.presetNames().indexOf("Default") >= 0) {
                        if (!config.loadPreset(name)) return
                    }
                    syncFromConfig()
                }

                Component.onCompleted: updateModel()
            }

            Button {
                text: "Save preset"
                flat: false
                palette.buttonText: "white"
                background: Rectangle { color: "#3060b0"; radius: 4 }
                onClicked: newPresetDialog.open()
                ToolTip.visible: hovered
                ToolTip.text: "Save current dock apps as a named preset"
            }

            Button {
                text: "Delete"
                flat: true
                palette.buttonText: "#ff8888"
                visible: config.presetNames().length > 0
                onClicked: {
                    const idx = presetCombo.currentIndex
                    const name = presetListModel.get(idx).name
                    config.deletePreset(name)
                    presetCombo.updateModel()
                }
                ToolTip.visible: hovered
                ToolTip.text: "Delete selected preset"
            }

            Button {
                text: "✕  Close"
                flat: false
                palette.buttonText: "white"
                background: Rectangle { color: "#444466"; radius: 4 }
                onClicked: root.close()
            }
        }
    }

    // ── Main two-column layout ───────────────────────────────────────────
    RowLayout {
        anchors {
            top: header.bottom
            left: parent.left; right: parent.right; bottom: parent.bottom
            margins: 0
        }
        spacing: 0

        // ── Left panel: current dock apps ────────────────────────────────
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width * 0.42
            color: "#1e1e38"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 0
                spacing: 0

                // Panel header
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color: "#25253d"
                    Text {
                        anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                        text: "Your Dock  ·  drag ≡ to reorder"
                        color: "#aab"
                        font.pixelSize: 12
                    }
                }

                // Drag-to-reorder list (wrapped in Item so the empty-state Text
                // can use anchors freely without conflicting with ColumnLayout)
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                ListView {
                    id: pinnedListView
                    anchors.fill: parent
                    clip: true
                    spacing: 0
                    model: DelegateModel {
                        id: visualModel
                        model: pinnedModel

                        delegate: DropArea {
                            id: dropDelegate
                            width: pinnedListView.width
                            height: 56

                            property int visualIndex: DelegateModel.itemsIndex

                            onEntered: (drag) => {
                                const from = drag.source.visualIndex
                                if (from !== dropDelegate.visualIndex)
                                    visualModel.items.move(from, dropDelegate.visualIndex)
                            }

                            Rectangle {
                                id: dragRect
                                width: pinnedListView.width
                                height: 56
                                color: dragHandler.active ? "#3a3a60" : (index % 2 === 0 ? "#1e1e38" : "#22223e")
                                border.color: dragHandler.active ? "#6666aa" : "transparent"
                                border.width: 1

                                Drag.active: dragHandler.active
                                Drag.source: dropDelegate
                                Drag.hotSpot: Qt.point(width / 2, height / 2)

                                states: State {
                                    when: dragRect.Drag.active
                                    ParentChange { target: dragRect; parent: pinnedListView }
                                    AnchorChanges {
                                        target: dragRect
                                        anchors.horizontalCenter: undefined
                                        anchors.verticalCenter: undefined
                                    }
                                }

                                RowLayout {
                                    anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                    spacing: 8

                                    // Drag handle
                                    Text {
                                        text: "≡"
                                        color: "#666"
                                        font.pixelSize: 18
                                        font.bold: true
                                    }

                                    // Icon
                                    Image {
                                        source: model.iconName ? "image://kdock/" + model.iconName : ""
                                        width: 32; height: 32
                                        fillMode: Image.PreserveAspectFit
                                        smooth: true
                                        asynchronous: true
                                    }

                                    // Name
                                    Text {
                                        Layout.fillWidth: true
                                        text: model.displayName || model.appId
                                        color: "white"
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                    }

                                    // Remove
                                    Button {
                                        text: "✕"
                                        flat: true
                                        implicitWidth: 28; implicitHeight: 28
                                        contentItem: Text { text: "✕"; color: "#ff8888"; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                        background: Rectangle { color: "transparent" }
                                        onClicked: {
                                            pinnedModel.remove(index)
                                            flushToConfig()
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Remove from dock"
                                    }
                                }

                                DragHandler {
                                    id: dragHandler
                                    onActiveChanged: if (!active) {
                                        flushToConfig()
                                    }
                                }
                            }
                        }
                    }
                }

                // Empty-state hint overlaid on the list area
                Text {
                    visible: pinnedModel.count === 0
                    anchors.centerIn: parent
                    text: "No apps in dock.\nSearch on the right to add some."
                    color: "#666"
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 13
                }
                } // Item wrapper
            }
        }

        // Divider
        Rectangle {
            Layout.fillHeight: true
            width: 1
            color: "#333355"
        }

        // ── Right panel: app library ──────────────────────────────────────
        ColumnLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: 0

            // Panel header + search box
            Rectangle {
                Layout.fillWidth: true
                height: 40
                color: "#25253d"

                RowLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    spacing: 8

                    Text { text: "🔍"; font.pixelSize: 14 }

                    TextField {
                        id: searchField
                        Layout.fillWidth: true
                        placeholderText: "Search apps to add…"
                        background: Rectangle { color: "transparent" }
                        color: "white"
                        font.pixelSize: 13
                        onTextChanged: appLibrary.filter = text.trim()
                        Keys.onEscapePressed: clear()
                    }

                    // Clear button
                    Text {
                        text: "✕"
                        color: searchField.text.length > 0 ? "#aaa" : "transparent"
                        font.pixelSize: 14
                        MouseArea { anchors.fill: parent; onClicked: searchField.clear() }
                    }
                }
            }

            // App list
            ListView {
                id: appListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 0
                model: appLibrary

                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: appRow
                    width: appListView.width
                    height: 52
                    color: mouseInside.containsMouse ? "#2a2a4a" : (index % 2 === 0 ? "#1a1a2e" : "#1e1e34")

                    readonly property bool alreadyPinned: {
                        for (let i = 0; i < pinnedModel.count; i++)
                            if (pinnedModel.get(i).appId === model.appId) return true
                        return false
                    }

                    RowLayout {
                        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                        spacing: 10

                        Image {
                            source: model.iconName ? "image://kdock/" + model.iconName : ""
                            width: 32; height: 32
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            asynchronous: true
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: model.displayName
                                color: "white"
                                font.pixelSize: 13
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: model.comment || model.category || model.appId
                                color: "#888"
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        Button {
                            implicitWidth: 68
                            implicitHeight: 28
                            enabled: !appRow.alreadyPinned
                            text: appRow.alreadyPinned ? "✓ Added" : "+ Add"
                            contentItem: Text {
                                text: parent.text
                                color: appRow.alreadyPinned ? "#88cc88" : "white"
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: appRow.alreadyPinned ? "#224422" : "#305090"
                                radius: 4
                                opacity: parent.enabled ? 1.0 : 0.6
                            }
                            onClicked: {
                                if (appRow.alreadyPinned) return
                                pinnedModel.append({
                                    appId:       model.appId,
                                    displayName: model.displayName,
                                    iconName:    model.iconName,
                                })
                                flushToConfig()
                            }
                        }
                    }

                    HoverHandler { id: mouseInside }
                }
            }
        }
    }
}
