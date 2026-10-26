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
    onShotStateChanged: {
        if (shotState === "apps-picker")
            picker.open()
        else if (shotState.startsWith("sites-input:"))
            input.text = shotState.slice(12)
        else if (shotState.startsWith("sites-open:"))
            toggleExpanded(shotState.slice(11))
    }
    readonly property bool general: Daemon.siteExclusionMode !== "selective"
    // Live check of what is typed in the add field; "domain.net" always brings "*.domain.net".
    readonly property var siteCheck: Daemon.checkSites(input.text, true)
    // Sites whose subdomain rows are unfolded.
    property var expanded: ({})
    function toggleExpanded(item) {
        const copy = Object.assign({}, expanded)
        copy[item] = !copy[item]
        expanded = copy
    }

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
                    SettingRow {
                        readonly property var early: Daemon.config["Exclusions TCP early ACK"]
                        visible: early !== undefined
                        title: qsTr("Recognize sites before connecting")
                        hint: qsTr("Makes the list work in browsers with their own secure DNS (DoH) from the first request.")
                        checked: early !== undefined && early.value === "on"
                        onToggled: (checked) => Daemon.setConfig("exclusions-tcp-early-ack", checked ? "on" : "off")
                    }
                }
            }

            ColumnLayout {
                spacing: 6
                Layout.fillWidth: true

                RowLayout {
                    spacing: 8
                    Layout.fillWidth: true
                    InputField {
                        id: input
                        iconName: "plus"
                        placeholderText: qsTr("example.com, .ru, 1.2.3.4, https://site.com/page")
                        Layout.fillWidth: true
                        onAccepted: add()
                        function add() {
                            if (page.siteCheck.entries.length === 0)
                                return
                            Daemon.addSiteExclusions(text, true)
                            text = ""
                        }
                    }
                    FlatButton {
                        kind: "primary"
                        text: qsTr("Add")
                        enabled: page.siteCheck.entries.length > 0 && Daemon.available
                        onClicked: input.add()
                    }
                }

                // What the input turns into, or why it cannot be added.
                Text {
                    visible: input.text.trim() !== "" && page.siteCheck.entries.length > 0
                    text: qsTr("Will add: %1").arg(page.siteCheck.entries.join(", "))
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Text {
                    visible: input.text.trim() !== "" && page.siteCheck.problems.length > 0
                    text: page.siteCheck.problems.join("\n")
                    color: Theme.danger
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }

            ListView {
                id: list
                model: Daemon.siteExclusionModel
                clip: true
                spacing: 0
                boundsBehavior: Flickable.StopAtBounds
                Layout.fillWidth: true
                Layout.fillHeight: true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    id: row
                    required property string item
                    required property bool on
                    required property string kind
                    required property string site
                    required property bool siteOn
                    required property int subCount
                    required property int subOn

                    readonly property bool child: site !== ""
                    // In effect: on, and under a site that is on.
                    readonly property bool active: on && siteOn
                    readonly property bool open: page.expanded[item] === true
                    readonly property string note: {
                        if (subCount > 0 && !open && kind !== "subdomains") {
                            if (subOn === subCount)
                                return qsTr("with subdomains")
                            return subOn === 0 ? qsTr("subdomains off") : qsTr("some subdomains off")
                        }
                        switch (kind) {
                        case "subdomains": return qsTr("all subdomains")
                        case "zone": return qsTr("the whole .%1 zone").arg(item.slice(2))
                        case "range": return qsTr("address range")
                        case "legacy": return qsTr("old format, may not work; remove and add again")
                        default: return ""
                        }
                    }

                    // Subdomain rows only show under an unfolded site.
                    visible: !child || page.expanded[site] === true
                    width: ListView.view.width
                    height: visible ? 44 : 0
                    radius: Theme.radiusSmall
                    color: rowMouse.containsMouse ? Theme.surfaceHover : "transparent"

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: row.subCount > 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: if (row.subCount > 0) page.toggleExpanded(row.item)
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: row.child ? 62 : 4
                        anchors.rightMargin: 4
                        spacing: 6

                        // Fold arrow for sites with subdomain rows; same width otherwise to keep alignment.
                        Item {
                            visible: !row.child
                            implicitWidth: 28
                            implicitHeight: 28
                            AppIcon {
                                anchors.centerIn: parent
                                visible: row.subCount > 0
                                name: "chevron"
                                size: 16
                                color: Theme.textSecondary
                                rotation: row.open ? 90 : 0
                                Behavior on rotation { NumberAnimation { duration: Theme.animFast } }
                            }
                        }
                        AppIcon {
                            name: "globe"
                            size: row.child ? 16 : 18
                            color: Theme.textSecondary
                            opacity: row.active ? 1 : 0.5
                        }
                        Text {
                            text: row.item
                            color: row.active ? Theme.text : Theme.textDisabled
                            font.pixelSize: row.child ? Theme.fontSmall + 1 : Theme.fontBody
                            font.strikeout: !row.active
                            elide: Text.ElideMiddle
                            Layout.leftMargin: 4
                            Layout.maximumWidth: row.width * 0.5
                        }
                        Text {
                            text: row.note
                            color: row.kind === "legacy" ? Theme.warning : Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                            Layout.leftMargin: 4
                            Layout.fillWidth: true
                        }
                        Toggle {
                            checked: row.on
                            // Under a switched-off site only the site's own switch matters.
                            enabled: row.siteOn
                            opacity: enabled ? 1 : 0.4
                            onToggled: (checked) => Daemon.setSiteExclusionEnabled(row.item, checked)
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
                visible: Daemon.appExclusionsSupported && Daemon.config["Mode"] !== undefined
                         && Daemon.config["Mode"].value === "socks"
                kind: "warning"
                title: qsTr("SOCKS5 mode: app exclusions do not apply")
                text: qsTr("Only programs set to use the proxy go through the VPN, everything else goes direct. Switch to TUN in Settings to use the list below.")
                Layout.fillWidth: true
            }

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
