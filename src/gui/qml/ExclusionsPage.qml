import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Agv
import Agv.Backend

Item {
    id: page

    // Development hook for --screenshot (AGV_SCREENSHOT_STATE=apps or apps-picker).
    readonly property string shotState: Window.window ? Window.window.screenshotState : ""
    property string tab: shotState.startsWith("apps") ? "apps" : "sites"
    onShotStateChanged: if (shotState === "apps-picker") picker.open()
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
                model: Daemon.siteExclusionModel
                clip: true
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds
                Layout.fillWidth: true
                Layout.fillHeight: true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: row
                    required property string item
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
                            text: row.item
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
                            onClicked: Daemon.removeSiteExclusion(row.item)
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
                    spacing: 10
                    Segmented {
                        options: [{ value: "bypass", label: qsTr("Bypass VPN") },
                                  { value: "vpn-only", label: qsTr("Only via VPN") }]
                        value: Daemon.appExclusionMode
                        onActivated: (value) => Daemon.setAppExclusionMode(value)
                        Layout.fillWidth: true
                    }
                    Text {
                        text: Daemon.appExclusionMode === "vpn-only"
                              ? qsTr("Only the apps below use the VPN. Everything else on this computer goes straight to the internet.")
                              : qsTr("The apps below go straight to the internet. Everything else uses the VPN.")
                        color: Daemon.appExclusionMode === "vpn-only" ? Theme.warning : Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }

            RowLayout {
                visible: Daemon.appExclusionsSupported
                spacing: 12
                Layout.fillWidth: true
                Text {
                    text: qsTr("Apps are matched however they are started: menu, terminal, Lutris, Steam.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                FlatButton {
                    kind: "primary"
                    iconName: "plus"
                    text: qsTr("Add app")
                    onClicked: picker.open()
                }
            }

            ListView {
                id: appList
                visible: Daemon.appExclusionsSupported
                model: Daemon.appModel
                clip: true
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds
                Layout.fillWidth: true
                Layout.fillHeight: true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: app
                    required property var model
                    readonly property var modelData: model
                    width: ListView.view.width
                    height: 60
                    radius: Theme.radiusSmall
                    color: appMouse.containsMouse ? Theme.surfaceHover : "transparent"

                    MouseArea {
                        id: appMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 4
                        spacing: 12

                        Image {
                            source: picker.iconSource(app.modelData.icon)
                            sourceSize: Qt.size(32, 32)
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            opacity: app.modelData.enabled ? 1 : 0.45
                        }
                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            Text {
                                text: app.modelData.name
                                color: app.modelData.enabled ? Theme.text : Theme.textSecondary
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                readonly property string what: app.modelData.cmdline_match !== ""
                                    ? qsTr("Command line: %1").arg(app.modelData.cmdline_match)
                                    : app.modelData.exe
                                readonly property real total: app.modelData.bytes_out + app.modelData.bytes_in
                                text: total > 0 ? "↑ %1  ↓ %2".arg(Theme.formatBytes(app.modelData.bytes_out))
                                                              .arg(Theme.formatBytes(app.modelData.bytes_in))
                                                : what
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                        Toggle {
                            checked: app.modelData.enabled
                            onToggled: (checked) => Daemon.setAppEnabled(app.modelData.id, checked)
                        }
                        FlatButton {
                            kind: "subtle"
                            iconName: "close"
                            compact: true
                            opacity: appMouse.containsMouse || hovered ? 1 : 0.5
                            onClicked: Daemon.removeApp(app.modelData.id)
                        }
                    }
                }

                Column {
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.spacing
                    visible: appList.count === 0
                    spacing: 6
                    Text {
                        width: parent.width
                        text: Daemon.appsLoaded ? qsTr("No apps yet") : qsTr("Loading…")
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Text {
                        visible: Daemon.appsLoaded
                        width: parent.width
                        text: Daemon.appExclusionMode === "vpn-only"
                              ? qsTr("Add the programs that should use the VPN. You can also start anything through it with: agv-run <command>")
                              : qsTr("Add a game or a program that should not use the VPN. You can also start anything outside the VPN with: agv-run <command>")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                }
            }

            Timer {
                interval: 2000
                repeat: true
                triggeredOnStart: true
                running: page.visible && page.tab === "apps" && Daemon.apps.length > 0
                onTriggered: Daemon.refreshAppTraffic()
            }
        }
    }

    AppPicker { id: picker }
}
