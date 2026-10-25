import QtQuick
import QtQuick.Layouts
import Agv
import Agv.Backend

// adguardvpn-cli settings: mode (TUN / SOCKS5), protocol, DNS and a few switches.
// They apply from the next connection; when connected, a reconnect button appears.
Card {
    id: card

    function setting(label) {
        const entry = Daemon.config[label]
        return entry ? entry.value : ""
    }
    function isDefault(label) {
        const entry = Daemon.config[label]
        return entry ? entry.isDefault : true
    }

    readonly property bool socks: setting("Mode") === "socks"
    readonly property string socksPort: setting("SOCKS port") || "1080"
    readonly property string socksHost: setting("SOCKS host") || "127.0.0.1"
    readonly property string protocol: setting("Protocol") || "auto"
    readonly property bool customDns: !isDefault("DNS upstream") && setting("DNS upstream") !== ""
    readonly property bool loaded: Object.keys(Daemon.config).length > 0

    Layout.fillWidth: true

    ColumnLayout {
        width: parent.width
        spacing: 14

        Text {
            text: qsTr("VPN")
            color: Theme.text
            font.pixelSize: Theme.fontBody
            font.weight: Font.DemiBold
        }

        Text {
            visible: !card.loaded
            text: Daemon.available ? qsTr("Loading…") : qsTr("The agvd service is not running")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
        }

        // Mode
        ColumnLayout {
            visible: card.loaded
            spacing: 6
            Layout.fillWidth: true
            RowLayout {
                spacing: 12
                Layout.fillWidth: true
                Text {
                    text: qsTr("Mode")
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    Layout.fillWidth: true
                }
                Segmented {
                    options: [{ value: "tun", label: "TUN" }, { value: "socks", label: "SOCKS5" }]
                    value: card.socks ? "socks" : "tun"
                    onActivated: (value) => Daemon.setConfig("mode", value)
                }
            }
            Text {
                text: card.socks
                      ? qsTr("Only programs set to use the proxy %1:%2 go through the VPN. App exclusions do not apply.")
                            .arg(card.socksHost).arg(card.socksPort)
                      : qsTr("All traffic of this computer goes through the VPN, except exclusions.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                visible: card.socks
                spacing: 8
                Layout.fillWidth: true
                Text {
                    text: qsTr("Proxy port")
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    Layout.fillWidth: true
                }
                InputField {
                    id: portField
                    text: card.socksPort
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 1; top: 65535 }
                    Layout.preferredWidth: 110
                    onAccepted: save()
                    onActiveFocusChanged: if (!activeFocus) save()
                    function save() {
                        if (acceptableInput && text !== card.socksPort)
                            Daemon.setConfig("socks-port", text)
                    }
                }
            }
        }

        // Protocol
        ColumnLayout {
            visible: card.loaded
            spacing: 6
            Layout.fillWidth: true
            RowLayout {
                spacing: 12
                Layout.fillWidth: true
                Text {
                    text: qsTr("Protocol")
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    Layout.fillWidth: true
                }
                Segmented {
                    options: [{ value: "auto", label: qsTr("Auto") },
                              { value: "http2", label: "HTTP/2" },
                              { value: "quic", label: "QUIC" }]
                    value: card.protocol
                    onActivated: (value) => Daemon.setConfig("protocol", value)
                }
            }
            Text {
                text: qsTr("QUIC (HTTP/3) is usually faster; HTTP/2 gets through networks that block it.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        // DNS
        ColumnLayout {
            visible: card.loaded
            spacing: 6
            Layout.fillWidth: true
            Text {
                text: qsTr("DNS server")
                color: Theme.text
                font.pixelSize: Theme.fontBody
            }
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                InputField {
                    id: dnsField
                    text: card.customDns ? card.setting("DNS upstream") : ""
                    placeholderText: qsTr("AdGuard VPN's own, or e.g. tls://dns.adguard-dns.com")
                    Layout.fillWidth: true
                    onAccepted: if (text.trim() !== "") Daemon.setConfig("dns", text.trim())
                }
                FlatButton {
                    visible: card.customDns
                    text: qsTr("Default")
                    compact: true
                    onClicked: Daemon.setConfig("dns", "default")
                }
            }
        }

        SettingRow {
            visible: card.loaded
            title: qsTr("Use the VPN's DNS for the whole system")
            hint: qsTr("Turn off to keep your own resolver (e.g. systemd-resolved settings).")
            // "not set" means the CLI default, which is on.
            checked: card.setting("Change system DNS") !== "off"
            onToggled: (checked) => Daemon.setConfig("change-system-dns", checked ? "on" : "off")
        }
        SettingRow {
            visible: card.loaded
            title: qsTr("Post-quantum encryption")
            hint: qsTr("Protects today's traffic against decryption by future quantum computers.")
            checked: card.setting("Post-quantum cryptography") !== "off"
            onToggled: (checked) => Daemon.setConfig("post-quantum", checked ? "on" : "off")
        }

        Banner {
            visible: Daemon.needsReconnect
            kind: "info"
            title: qsTr("Reconnect to apply the new settings")
            actionText: qsTr("Reconnect")
            onAction: Daemon.reconnect()
            Layout.fillWidth: true
        }
    }
}
