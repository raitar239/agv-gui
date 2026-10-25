import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Agv
import Agv.Backend

Item {
    id: page

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 2 * Theme.spacing
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: column
            x: Theme.spacing
            y: Theme.spacing + 4
            width: Math.min(page.width - 2 * Theme.spacing, 560)
            spacing: 12

            Text {
                text: qsTr("Settings")
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
            }

            // Account
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    spacing: 10

                    RowLayout {
                        spacing: 12
                        Layout.fillWidth: true
                        Rectangle {
                            width: 40
                            height: 40
                            radius: 20
                            color: Theme.accentSoft
                            AppIcon { anchors.centerIn: parent; name: "user"; color: Theme.accent }
                        }
                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            Text {
                                text: Daemon.loggedIn ? (Daemon.license.account || qsTr("Signed in"))
                                                      : qsTr("Not signed in")
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                visible: Daemon.loggedIn && Daemon.license.plan !== undefined
                                text: qsTr("%1 · up to %2 devices · until %3")
                                      .arg(Daemon.license.plan || "")
                                      .arg(Daemon.license.max_devices || "?")
                                      .arg(Daemon.license.valid_until || "—")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        Rectangle {
                            visible: Daemon.license.plan === "PREMIUM"
                            color: Theme.accentSoft
                            radius: 10
                            implicitWidth: badge.implicitWidth + 16
                            implicitHeight: 22
                            Text {
                                id: badge
                                anchors.centerIn: parent
                                text: "PREMIUM"
                                color: Theme.accent
                                font.pixelSize: 10
                                font.weight: Font.Bold
                            }
                        }
                    }

                    Flow {
                        spacing: 8
                        Layout.fillWidth: true
                        FlatButton {
                            visible: !Daemon.loggedIn
                            kind: "primary"
                            iconName: "login"
                            text: Daemon.loginPending ? qsTr("Waiting for browser…") : qsTr("Sign in")
                            enabled: !Daemon.loginPending && Daemon.available
                            onClicked: Daemon.login()
                        }
                        FlatButton {
                            visible: Daemon.loginPending
                            text: qsTr("Cancel")
                            onClicked: Daemon.cancelLogin()
                        }
                        FlatButton {
                            visible: !Daemon.loggedIn
                            iconName: "download"
                            text: qsTr("Use adguardvpn-cli login")
                            enabled: Daemon.available
                            onClicked: Daemon.importLogin()
                        }
                        FlatButton {
                            visible: Daemon.loggedIn
                            iconName: "logout"
                            text: qsTr("Sign out")
                            enabled: !Daemon.busy
                            onClicked: Daemon.logout()
                        }
                    }
                }
            }

            // Appearance
            Card {
                Layout.fillWidth: true
                RowLayout {
                    width: parent.width
                    spacing: 12
                    AppIcon { name: "palette"; color: Theme.textSecondary }
                    Text {
                        text: qsTr("Theme")
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        Layout.fillWidth: true
                    }
                    Segmented {
                        options: [{ value: "system", label: qsTr("System") },
                                  { value: "light", label: qsTr("Light") },
                                  { value: "dark", label: qsTr("Dark") }]
                        value: SystemTheme.preference
                        onActivated: (value) => SystemTheme.preference = value
                    }
                }
            }

            // Service
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    spacing: 6
                    RowLayout {
                        spacing: 8
                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: Daemon.available ? Theme.accent : Theme.danger
                        }
                        Text {
                            text: Daemon.available ? qsTr("agvd service is running") : qsTr("agvd service is not running")
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                        }
                    }
                    Text {
                        text: qsTr("App exclusions: %1").arg(Daemon.appExclusionsSupported ? qsTr("available") : qsTr("not available"))
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                    Text {
                        text: qsTr("agv-gui %1").arg(Qt.application.version)
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }
        }
    }
}
