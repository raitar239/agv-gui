import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Agv
import Agv.Backend

// Choose an installed program, or describe one (Wine games) by its command line.
Popup {
    id: root

    property string tab: "installed"

    function iconSource(icon) {
        return "image://appicon/" + encodeURIComponent(icon || "")
    }

    modal: true
    focus: true
    padding: 0
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent.width - 24, 520)
    height: Math.min(parent.height - 24, 620)

    onOpened: {
        installed.reload()
        search.text = ""
        search.forceActiveFocus()
    }

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.45) }

    background: Rectangle {
        color: Theme.bg
        radius: Theme.radius
        border.width: 1
        border.color: Theme.border
    }

    InstalledApps { id: installed }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacing
            Layout.bottomMargin: 0
            Text {
                text: qsTr("Add an app")
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
                Layout.fillWidth: true
            }
            FlatButton {
                kind: "subtle"
                iconName: "close"
                compact: true
                onClicked: root.close()
            }
        }

        Segmented {
            options: [{ value: "installed", label: qsTr("Installed") },
                      { value: "custom", label: qsTr("Wine or other") }]
            value: root.tab
            onActivated: (value) => root.tab = value
            Layout.leftMargin: Theme.spacing
            Layout.rightMargin: Theme.spacing
            Layout.fillWidth: true
        }

        // Installed applications
        ColumnLayout {
            visible: root.tab === "installed"
            spacing: 8
            Layout.fillWidth: true
            Layout.fillHeight: true

            InputField {
                id: search
                iconName: "search"
                placeholderText: qsTr("Search apps")
                onTextChanged: installed.filter = text
                Layout.leftMargin: Theme.spacing
                Layout.rightMargin: Theme.spacing
                Layout.fillWidth: true
            }

            ListView {
                id: list
                model: installed
                clip: true
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                Layout.bottomMargin: 8
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: row
                    required property var model
                    // Reading apps makes this re-run when the list changes.
                    readonly property bool added: Daemon.apps.length >= 0 && Daemon.isAppExcluded(model.desktopFile, model.exe)
                    readonly property bool usable: !model.flatpak && !added

                    width: ListView.view.width
                    height: 52
                    radius: Theme.radiusSmall
                    color: rowMouse.containsMouse && row.usable ? Theme.surfaceHover : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 12
                        Image {
                            source: root.iconSource(row.model.icon)
                            sourceSize: Qt.size(32, 32)
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            opacity: row.usable ? 1 : 0.5
                        }
                        ColumnLayout {
                            spacing: 1
                            Layout.fillWidth: true
                            Text {
                                text: row.model.name
                                color: row.usable ? Theme.text : Theme.textDisabled
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                text: row.model.flatpak ? qsTr("Flatpak apps are not supported yet") : row.model.exe
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                        Text {
                            visible: row.added
                            text: qsTr("Added")
                            color: Theme.accent
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                        }
                        AppIcon {
                            visible: row.usable
                            name: "plus"
                            size: 18
                            color: rowMouse.containsMouse ? Theme.accent : Theme.textSecondary
                        }
                    }
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: row.usable
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            Daemon.addApp({ name: row.model.name, icon: row.model.icon,
                                            desktop_file: row.model.desktopFile, exe: row.model.exe })
                            root.close()
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: qsTr("No apps found")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                }
            }
        }

        // Custom rule
        ColumnLayout {
            visible: root.tab === "custom"
            spacing: 10
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spacing
            Layout.rightMargin: Theme.spacing
            Layout.bottomMargin: Theme.spacing

            Text {
                text: qsTr("For Wine and Proton games the program is wine64-preloader, so match a part of the command line instead, e.g. the game's .exe name. Everything the matched process starts is excluded too.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            InputField {
                id: customName
                placeholderText: qsTr("Name, e.g. Genshin Impact")
                Layout.fillWidth: true
            }
            InputField {
                id: customCmdline
                placeholderText: qsTr("Command line contains, e.g. GenshinImpact.exe")
                Layout.fillWidth: true
            }
            InputField {
                id: customExe
                placeholderText: qsTr("Or program path, e.g. /usr/bin/lutris")
                Layout.fillWidth: true
            }
            FlatButton {
                kind: "primary"
                text: qsTr("Add")
                enabled: customName.text.trim() !== ""
                         && (customCmdline.text.trim() !== "" || customExe.text.trim().startsWith("/"))
                onClicked: {
                    Daemon.addApp({ name: customName.text.trim(), cmdline_match: customCmdline.text.trim(),
                                    exe: customExe.text.trim() })
                    customName.text = ""
                    customCmdline.text = ""
                    customExe.text = ""
                    root.close()
                }
                Layout.alignment: Qt.AlignRight
            }
            Item { Layout.fillHeight: true }
        }
    }
}
