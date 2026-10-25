import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Agv
import Agv.Backend

Item {
    id: page

    property bool compactHeight: false
    property bool showLocationButton: true
    signal openLocations()

    readonly property bool working: Daemon.busy || Daemon.transitioning
    readonly property string currentCity: prettyCity(Daemon.location)
    // Reading total makes these re-run once the location list arrives.
    readonly property string currentCountry: Daemon.locations.total >= 0 ? Daemon.locations.countryOf(currentCity) : ""
    // "Country, City", or just the city until the list is known.
    readonly property string currentPlace: currentCountry !== "" ? currentCountry + ", " + currentCity : currentCity

    function prettyCity(name) {
        // The CLI reports "ATHENS"; show "Athens".
        return name.toLowerCase().replace(/(^|[\s-])\S/g, function (s) { return s.toUpperCase() })
    }

    Flickable {
        anchors.fill: parent
        contentHeight: Math.max(height, column.implicitHeight + 2 * Theme.spacing)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: column
            width: Math.min(page.width - 2 * Theme.spacing, 460)
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(Theme.spacing, (parent.height - implicitHeight) / 2)
            spacing: page.compactHeight ? 10 : Theme.spacing

            Banner {
                visible: !Daemon.available
                kind: "danger"
                title: qsTr("The agvd service is not running")
                text: qsTr("Start it with: sudo systemctl start agvd")
                Layout.fillWidth: true
            }
            Banner {
                visible: Daemon.available && Daemon.externalVpnCount > 0 && !Daemon.connected
                title: qsTr("AdGuard VPN is already running outside this app")
                text: qsTr("Take it over to control it from here and use app exclusions.")
                actionText: qsTr("Take over")
                onAction: Daemon.takeOver()
                Layout.fillWidth: true
            }
            Banner {
                visible: Daemon.available && !Daemon.loggedIn && Daemon.state !== "unknown"
                kind: "info"
                title: qsTr("Sign in to AdGuard VPN")
                text: Daemon.loginPending ? qsTr("Waiting for confirmation in your browser…")
                                          : qsTr("Use your AdGuard account, or the one adguardvpn-cli already uses.")
                actionText: Daemon.loginPending ? "" : qsTr("Sign in")
                onAction: Daemon.login()
                Layout.fillWidth: true
            }

            ConnectButton {
                diameter: Math.max(120, Math.min(page.width * 0.5, page.height * (page.compactHeight ? 0.42 : 0.36), 220))
                connected: Daemon.connected
                working: page.working
                enabledState: Daemon.available && Daemon.loggedIn
                onClicked: Daemon.toggle()
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: page.compactHeight ? 0 : 8
            }

            ColumnLayout {
                spacing: 4
                Layout.fillWidth: true
                Text {
                    text: page.working ? (Daemon.state === "disconnecting" ? qsTr("Disconnecting…") : qsTr("Connecting…"))
                                       : Daemon.connected ? qsTr("Protected") : qsTr("Not protected")
                    color: Daemon.connected && !page.working ? Theme.accent : Theme.text
                    font.pixelSize: Theme.fontHeadline
                    font.weight: Font.Bold
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }
                Text {
                    text: {
                        if (Daemon.state === "error" && Daemon.lastError !== "")
                            return Daemon.lastError
                        if (Daemon.connected)
                            return qsTr("Your traffic goes through %1").arg(page.currentPlace)
                        return Daemon.loggedIn ? qsTr("Press the button to connect") : ""
                    }
                    color: Daemon.state === "error" ? Theme.danger : Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }

            Card {
                visible: page.showLocationButton && Daemon.loggedIn
                padding: 0
                implicitHeight: 64
                Layout.fillWidth: true
                Layout.topMargin: page.compactHeight ? 0 : 8

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing
                    anchors.rightMargin: 8
                    spacing: 12

                    Text {
                        text: Daemon.connected && Daemon.locations.total >= 0 ? Daemon.locations.flagOf(page.currentCity) : ""
                        visible: text !== ""
                        font.pixelSize: 26
                    }
                    AppIcon {
                        visible: !Daemon.connected
                        name: "globe"
                        color: Theme.textSecondary
                    }
                    ColumnLayout {
                        spacing: 1
                        Layout.fillWidth: true
                        Text {
                            text: Daemon.connected ? (page.currentCountry || page.currentCity) : qsTr("Choose a location")
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            readonly property int ping: Daemon.locations.total >= 0 ? Daemon.locations.pingOf(page.currentCity) : -1
                            text: {
                                if (Daemon.connected) {
                                    const city = page.currentCountry !== "" ? page.currentCity : ""
                                    const details = ping >= 0 ? qsTr("Ping %1 ms").arg(ping) : qsTr("Connected")
                                    return city !== "" ? city + " · " + details : details
                                }
                                return Daemon.locations.total > 0 ? qsTr("%1 locations").arg(Daemon.locations.total)
                                                                  : qsTr("Pick a server")
                            }
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            Layout.fillWidth: true
                        }
                    }
                    FlatButton {
                        kind: "ghost"
                        iconName: "bolt"
                        text: page.width > 380 ? qsTr("Fastest") : ""
                        compact: true
                        enabled: !page.working && Daemon.available
                        onClicked: Daemon.connectFastest()
                    }
                    FlatButton {
                        kind: "ghost"
                        iconName: "chevron"
                        compact: true
                        onClicked: page.openLocations()
                    }
                }
            }

            // Favorite locations, one tap away.
            Flow {
                visible: Daemon.loggedIn && Daemon.locations.favorites.length > 0
                spacing: 8
                Layout.fillWidth: true
                Layout.topMargin: page.compactHeight ? 0 : 4

                Repeater {
                    model: Daemon.locations.favorites
                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        readonly property bool current: Daemon.connected
                                                        && modelData.city.toUpperCase() === Daemon.location.toUpperCase()

                        height: 34
                        width: chipRow.implicitWidth + 24
                        radius: 17
                        color: current ? Theme.accentSoft : chipMouse.containsMouse ? Theme.surfaceHover : Theme.surface
                        border.width: 1
                        border.color: current ? Theme.accentSoftBorder : Theme.border

                        Row {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: 6
                            Text {
                                text: chip.modelData.flag
                                font.pixelSize: 16
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: chip.modelData.country + ", " + chip.modelData.city
                                color: chip.current ? Theme.accent : Theme.text
                                font.pixelSize: Theme.fontSmall + 1
                                font.weight: chip.current ? Font.DemiBold : Font.Normal
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        MouseArea {
                            id: chipMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            enabled: !chip.current && !page.working
                            onClicked: Daemon.connectTo(chip.modelData.city)
                        }
                    }
                }
            }
        }
    }
}
