import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Agv
import Agv.Backend

Item {
    id: page

    signal copied()

    // AdGuard's official installer (github.com/AdguardTeam/AdGuardVPNCLI).
    readonly property string cliInstallCommand:
        "curl -fsSL https://raw.githubusercontent.com/AdguardTeam/AdGuardVPNCLI/master/scripts/release/install.sh | sh -s -- -v"

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

            VpnSettingsCard {}

            // Protection
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    spacing: 14
                    Text {
                        text: qsTr("Protection")
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                    }
                    SettingRow {
                        title: qsTr("Kill switch")
                        hint: qsTr("If the VPN drops, block the internet until it reconnects, so nothing leaves without it. Apps excluded from the VPN keep working. Turning the VPN off yourself unblocks it.")
                        checked: Daemon.killSwitch
                        available: Daemon.available && Daemon.appExclusionsSupported
                        onToggled: (checked) => Daemon.setKillSwitch(checked)
                    }
                    SettingRow {
                        visible: Daemon.killSwitch
                        title: qsTr("Allow the local network")
                        hint: qsTr("Printers, file shares and the router stay reachable while the internet is blocked.")
                        checked: Daemon.killSwitchAllowLan
                        available: Daemon.available
                        onToggled: (checked) => Daemon.setKillSwitchAllowLan(checked)
                    }
                    SettingRow {
                        title: qsTr("Keep DNS inside the VPN")
                        hint: qsTr("Sends every program's DNS lookups through the VPN, even where the system asks the router, so your internet provider does not see the sites you open. Local names like router.lan do not resolve while connected.")
                        checked: Daemon.keepDnsInVpn
                        available: Daemon.available && Daemon.appExclusionsSupported
                        onToggled: (checked) => Daemon.setKeepDnsInVpn(checked)
                    }
                    SettingRow {
                        title: qsTr("Notify when the connection drops")
                        hint: qsTr("A desktop notification when the VPN is lost and when it is back.")
                        checked: Notifier.enabled
                        onToggled: (checked) => Notifier.enabled = checked
                    }
                }
            }

            // Networks
            Card {
                readonly property var network: Daemon.network
                visible: Daemon.available && network.available === true
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    spacing: 14
                    Text {
                        text: qsTr("Networks")
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                    }
                    SettingRow {
                        title: qsTr("Connect on Wi-Fi I do not trust")
                        hint: qsTr("Cafés, hotels, airports: the VPN turns on when the computer joins any Wi-Fi that is not in the trusted list.")
                        checked: Daemon.wifiAutoConnect
                        available: Daemon.loggedIn
                        onToggled: (checked) => Daemon.setWifiAutoConnect(checked)
                    }
                    SettingRow {
                        title: qsTr("Disconnect on trusted networks")
                        hint: qsTr("The VPN turns off when the computer joins a trusted network, at home for example.")
                        checked: Daemon.trustedDisconnect
                        onToggled: (checked) => Daemon.setTrustedDisconnect(checked)
                    }

                    // The network the computer is on now.
                    RowLayout {
                        readonly property var network: Daemon.network
                        visible: network.type !== undefined && network.type !== ""
                        spacing: 12
                        Layout.fillWidth: true
                        AppIcon {
                            name: parent.network.type === "wifi" ? "wifi" : "globe"
                            color: Theme.textSecondary
                        }
                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            Text {
                                text: parent.parent.network.name || ""
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                readonly property var network: parent.parent.network
                                text: network.trusted ? qsTr("Current network · trusted")
                                    : network.open ? qsTr("Current network · open Wi-Fi without a password")
                                    : qsTr("Current network")
                                color: network.open && !network.trusted ? Theme.warning : Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                Layout.fillWidth: true
                            }
                        }
                        FlatButton {
                            readonly property var network: parent.network
                            kind: network.trusted ? "subtle" : "secondary"
                            compact: true
                            text: network.trusted ? qsTr("Don't trust") : qsTr("Trust")
                            onClicked: Daemon.setNetworkTrusted(network.uuid, network.name, !network.trusted)
                        }
                    }

                    // Other trusted networks, to forget them.
                    Repeater {
                        model: Daemon.trustedNetworks.filter(n => n.uuid !== Daemon.network.uuid)
                        delegate: RowLayout {
                            required property var modelData
                            spacing: 12
                            Layout.fillWidth: true
                            AppIcon { name: "check"; color: Theme.accent; size: 18 }
                            Text {
                                text: modelData.name
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall + 1
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            FlatButton {
                                kind: "subtle"
                                compact: true
                                iconName: "close"
                                onClicked: Daemon.setNetworkTrusted(modelData.uuid, modelData.name, false)
                            }
                        }
                    }
                }
            }

            // Appearance
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    RowLayout {
                        spacing: 12
                        Layout.fillWidth: true
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
                    RowLayout {
                        spacing: 12
                        Layout.fillWidth: true
                        AppIcon { name: "globe"; color: Theme.textSecondary }
                        Text {
                            text: qsTr("Language")
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                        }
                        Dropdown {
                            options: Language.options
                            value: Language.preference
                            onActivatedValue: (value) => Language.preference = value
                        }
                    }
                }
            }

            // Startup
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    spacing: 14

                    Text {
                        text: qsTr("Startup")
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.DemiBold
                    }
                    SettingRow {
                        title: qsTr("Open AGV in the tray when I log in")
                        checked: Autostart.enabled
                        onToggled: (checked) => Autostart.enabled = checked
                    }
                    // Hyprland does not run XDG autostart entries by itself: offer the config line.
                    ColumnLayout {
                        visible: Autostart.compositorLine !== ""
                        spacing: 6
                        Layout.fillWidth: true
                        Layout.topMargin: -6
                        Text {
                            text: qsTr("Hyprland: add this line to hyprland.conf. Click to copy.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Rectangle {
                            implicitHeight: 34
                            radius: Theme.radiusSmall
                            color: copyMouse.containsMouse ? Theme.surfaceHover : (Theme.dark ? "#151515" : "#f3f3f3")
                            border.width: 1
                            border.color: copyMouse.containsMouse ? Theme.accentSoftBorder : Theme.border
                            Layout.fillWidth: true
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 10
                                Text {
                                    text: Autostart.compositorLine
                                    color: Theme.text
                                    font.family: "monospace"
                                    font.pixelSize: Theme.fontSmall + 1
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: qsTr("Copy")
                                    color: Theme.accent
                                    font.pixelSize: Theme.fontSmall
                                    font.weight: Font.DemiBold
                                }
                            }
                            MouseArea {
                                id: copyMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    Autostart.copyToClipboard(Autostart.compositorLine)
                                    page.copied()
                                }
                            }
                        }
                    }
                    SettingRow {
                        title: qsTr("Start the VPN service at boot")
                        hint: qsTr("Needs an administrator password.")
                        checked: Daemon.startAtBoot
                        available: Daemon.available
                        onToggled: (checked) => Daemon.setStartAtBoot(checked)
                    }
                    SettingRow {
                        title: qsTr("Connect automatically")
                        hint: qsTr("When the service starts, connect to the last location and keep trying until the network is up.")
                        checked: Daemon.autoConnect
                        available: Daemon.available && Daemon.loggedIn
                        onToggled: (checked) => Daemon.setAutoConnect(checked)
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

                    // The CLI underneath, and its updates.
                    RowLayout {
                        id: cliRow
                        readonly property bool outdated: Daemon.cliVersion !== ""
                                                         && CliUpdates.isNewer(Daemon.cliVersion, CliUpdates.latest)
                        visible: Daemon.available && Daemon.cliInstall !== ""
                        spacing: 12
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            Text {
                                text: Daemon.cliInstall === "missing" ? qsTr("AdGuard VPN CLI is not installed")
                                                                      : qsTr("AdGuard VPN CLI %1").arg(Daemon.cliVersion)
                                color: Daemon.cliInstall === "missing" ? Theme.danger : Theme.text
                                font.pixelSize: Theme.fontBody
                            }
                            Text {
                                text: {
                                    if (Daemon.cliInstall === "missing")
                                        return qsTr("AGV needs it to connect. Install it, and AGV finds it on its own.")
                                    if (cliRow.outdated)
                                        return Daemon.cliInstall === "script"
                                            ? qsTr("Version %1 is available.").arg(CliUpdates.latest)
                                            : qsTr("Version %1 is available. Update it with your package manager.").arg(CliUpdates.latest)
                                    if (CliUpdates.checking)
                                        return qsTr("Checking for updates…")
                                    return CliUpdates.latest !== "" ? qsTr("Up to date") : ""
                                }
                                visible: text !== ""
                                color: cliRow.outdated ? Theme.accent : Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                        FlatButton {
                            visible: cliRow.outdated && Daemon.cliInstall === "script"
                            kind: "primary"
                            compact: true
                            text: qsTr("Update")
                            enabled: !Daemon.busy
                            onClicked: Daemon.updateCli()
                        }
                        FlatButton {
                            visible: Daemon.cliInstall === "missing"
                            kind: "secondary"
                            compact: true
                            text: qsTr("Copy install command")
                            onClicked: {
                                Autostart.copyToClipboard(page.cliInstallCommand)
                                page.copied()
                            }
                        }
                        FlatButton {
                            visible: !cliRow.outdated && Daemon.cliInstall !== "missing"
                            kind: "subtle"
                            compact: true
                            iconName: "refresh"
                            enabled: !CliUpdates.checking
                            onClicked: CliUpdates.check()
                        }
                    }
                }
            }
        }
    }
}
