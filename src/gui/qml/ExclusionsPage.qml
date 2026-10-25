import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Agv
import Agv.Backend

Item {
    id: page

    property string tab: "sites"
    readonly property bool general: Daemon.siteExclusionMode !== "selective"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        anchors.topMargin: Theme.spacing + 4
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Exclusions")
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
                Layout.fillWidth: true
            }
            Segmented {
                options: [{ value: "sites", label: qsTr("Websites") }, { value: "apps", label: qsTr("Apps") }]
                value: page.tab
                onActivated: (value) => page.tab = value
            }
        }

        // Websites
        ColumnLayout {
            visible: page.tab === "sites"
            spacing: 12
            Layout.fillWidth: true
            Layout.fillHeight: true

            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    spacing: 10
                    Segmented {
                        options: [{ value: "general", label: qsTr("General") },
                                  { value: "selective", label: qsTr("Selective") }]
                        value: page.general ? "general" : "selective"
                        onActivated: (value) => Daemon.setSiteExclusionMode(value)
                        Layout.fillWidth: true
                    }
                    Text {
                        text: page.general
                              ? qsTr("VPN works everywhere except the websites below.")
                              : qsTr("VPN works only for the websites below.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }

            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                InputField {
                    id: input
                    iconName: "plus"
                    placeholderText: qsTr("example.com, *.example.org, 1.2.3.4")
                    Layout.fillWidth: true
                    onAccepted: add()
                    function add() {
                        if (text.trim() === "")
                            return
                        Daemon.addSiteExclusions(text)
                        text = ""
                    }
                }
                FlatButton {
                    kind: "primary"
                    text: qsTr("Add")
                    enabled: input.text.trim() !== "" && Daemon.available
                    onClicked: input.add()
                }
            }

            ListView {
                id: list
                model: Daemon.siteExclusions
                clip: true
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds
                Layout.fillWidth: true
                Layout.fillHeight: true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: row
                    required property string modelData
                    width: ListView.view.width
                    height: 44
                    radius: Theme.radiusSmall
                    color: rowMouse.containsMouse ? Theme.surfaceHover : "transparent"

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 4
                        AppIcon { name: "globe"; size: 18; color: Theme.textSecondary }
                        Text {
                            text: row.modelData
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        FlatButton {
                            kind: "subtle"
                            iconName: "close"
                            compact: true
                            opacity: rowMouse.containsMouse || hovered ? 1 : 0.5
                            onClicked: Daemon.removeSiteExclusion(row.modelData)
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.spacing
                    visible: list.count === 0
                    text: Daemon.siteExclusionsLoaded ? qsTr("No websites yet. Add a domain above.")
                                                      : qsTr("Loading…")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }
        }

        // Apps
        ColumnLayout {
            visible: page.tab === "apps"
            spacing: 12
            Layout.fillWidth: true
            Layout.fillHeight: true

            Banner {
                visible: !Daemon.appExclusionsSupported
                kind: "warning"
                title: qsTr("App exclusions are not available")
                text: qsTr("They need cgroup v2, nftables and the agvd service running as root.")
                Layout.fillWidth: true
            }
            Card {
                visible: Daemon.appExclusionsSupported
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    spacing: 8
                    Text {
                        text: qsTr("Run a program outside the VPN")
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: qsTr("Start it with agv-run. Everything it launches stays outside the VPN too, e.g. a game started from Lutris or Steam.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Rectangle {
                        color: Theme.dark ? "#151515" : "#f3f3f3"
                        radius: Theme.radiusSmall
                        implicitHeight: 36
                        Layout.fillWidth: true
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            text: "agv-run lutris"
                            color: Theme.text
                            font.family: "monospace"
                            font.pixelSize: Theme.fontBody
                        }
                    }
                    Text {
                        text: qsTr("Choosing apps from a list arrives in the next version.")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }
}
