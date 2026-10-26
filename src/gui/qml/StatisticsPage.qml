import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Agv
import Agv.Backend

Item {
    id: page

    property string period: shotState.startsWith("stats:") ? shotState.slice(6) : "today"
    // Development hook for --screenshot (AGV_SCREENSHOT_STATE=stats:week and so on).
    readonly property string shotState: Window.window ? Window.window.screenshotState : ""
    readonly property bool wide: width >= 760
    readonly property bool narrow: width < 520

    // Re-summarized when agvd sends new numbers, the period changes or the language switches.
    readonly property var stats: {
        Daemon.statisticsRevision
        Language.preference
        return Daemon.statisticsFor(period)
    }
    // In SOCKS5 mode the speed comes from agvd's counters for the CLI (they need nftables).
    // An app picked in the Apps card: the chart shows its traffic.
    property string selectedApp
    property string selectedAppName
    readonly property var chartStats: {
        if (selectedApp === "")
            return stats
        Daemon.statisticsRevision
        Language.preference
        return Daemon.statisticsFor(period, selectedApp)
    }
    function selectApp(id, name) {
        selectedApp = selectedApp === id ? "" : id
        selectedAppName = name
    }

    readonly property bool liveTraffic: (Daemon.connected && (Daemon.hasTunnel || Daemon.appExclusionsSupported))
                                        || shotState === "stats-live"

    // Live speed from the tunnel counters, one point per second for the last minute.
    property var speedIn: []
    property var speedOut: []
    property real lastIn: -1
    property real lastOut: -1
    property real lastTime: 0
    readonly property real currentIn: speedIn.length > 0 ? speedIn[speedIn.length - 1] : 0
    readonly property real currentOut: speedOut.length > 0 ? speedOut[speedOut.length - 1] : 0

    function resetSpeed() {
        speedIn = []
        speedOut = []
        lastIn = -1
        lastOut = -1
        // Screenshots of the live card without a VPN connection.
        if (shotState === "stats-live") {
            for (let i = 0; i < 60; ++i) {
                speedIn.push(1.5e6 + 1.2e6 * Math.sin(i / 6) + 4e5 * Math.sin(i * 1.7))
                speedOut.push(1.2e5 + 6e4 * Math.cos(i / 5))
            }
            speedIn = speedIn.slice()
            speedOut = speedOut.slice()
        }
    }

    function prettyCity(name) {
        return name.toLowerCase().replace(/(^|[\s-])\S/g, function (s) { return s.toUpperCase() })
    }
    function locationTitle(location) {
        if (location === "")
            return qsTr("Unknown location")
        const city = prettyCity(location)
        if (Daemon.locations.total < 0)
            return city
        const country = Daemon.locations.countryOf(city)
        const name = Daemon.locations.cityName(city)
        return country !== "" ? country + " · " + name : name
    }
    function flag(location) {
        return location !== "" && Daemon.locations.total >= 0 ? Daemon.locations.flagOf(prettyCity(location)) : ""
    }
    function appName(app) {
        return app.id === "run" ? qsTr("Started with agv-run") : app.name
    }

    Connections {
        target: Daemon
        function onTunnelTrafficChanged() {
            if (!page.visible)
                return
            const now = Date.now()
            if (page.lastIn >= 0 && now > page.lastTime) {
                const seconds = (now - page.lastTime) / 1000
                const down = Math.max(0, Daemon.tunnelBytesIn - page.lastIn) / seconds
                const up = Math.max(0, Daemon.tunnelBytesOut - page.lastOut) / seconds
                page.speedIn = page.speedIn.concat([down]).slice(-60)
                page.speedOut = page.speedOut.concat([up]).slice(-60)
            }
            page.lastIn = Daemon.tunnelBytesIn
            page.lastOut = Daemon.tunnelBytesOut
            page.lastTime = now
        }
        function onStatusChanged() {
            if (!Daemon.connected)
                page.resetSpeed()
        }
    }
    onVisibleChanged: if (!visible) resetSpeed()
    onShotStateChanged: {
        resetSpeed()
        if (shotState.startsWith("stats-app:"))
            selectApp(shotState.slice(10), "Genshin Impact")
    }

    Timer {
        interval: 1000
        repeat: true
        triggeredOnStart: true
        running: page.visible && page.liveTraffic
        onTriggered: Daemon.refreshTunnelTraffic()
    }
    Timer {
        interval: 5000
        repeat: true
        triggeredOnStart: true
        running: page.visible && Daemon.available
        onTriggered: Daemon.refreshStatistics()
    }

    // A figure with a caption and a detail line.
    component Tile: Card {
        id: tile
        property string caption
        property string value
        property string detail
        property string iconName
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        ColumnLayout {
            width: parent.width
            spacing: 4
            RowLayout {
                spacing: 6
                AppIcon { name: tile.iconName; size: 16; color: Theme.textSecondary }
                Text {
                    text: tile.caption
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            Text {
                text: tile.value
                color: Theme.text
                font.pixelSize: Theme.fontHeadline - 2
                font.weight: Font.Bold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: tile.detail
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }

    // Name, a bar for its share and the numbers on the right.
    component ShareRow: Item {
        id: share
        property bool selected: false
        property bool clickable: false
        signal clicked()
        property string title
        property string leading      // flag emoji
        property url iconSource
        property real fraction
        property string figure
        property string detail
        implicitHeight: shareColumn.implicitHeight
        Layout.fillWidth: true

        MouseArea {
            anchors.fill: parent
            anchors.margins: -4
            enabled: share.clickable
            cursorShape: share.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: share.clicked()
        }
        ColumnLayout {
            id: shareColumn
            width: parent.width
            spacing: 6
            RowLayout {
                spacing: 10
                Layout.fillWidth: true
                Text {
                    visible: share.leading !== ""
                    text: share.leading
                    font.pixelSize: 18
                }
                Image {
                    visible: share.iconSource.toString() !== ""
                    source: share.iconSource
                    sourceSize: Qt.size(22, 22)
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                }
                Text {
                    text: share.title
                    color: share.selected ? Theme.accent : Theme.text
                    font.pixelSize: Theme.fontBody
                    font.weight: share.selected ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: share.figure
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall + 1
                    font.weight: Font.DemiBold
                }
            }
            RowLayout {
                spacing: 10
                Layout.fillWidth: true
                Rectangle {
                    height: 6
                    radius: 3
                    color: Theme.chartGrid
                    Layout.fillWidth: true
                    Rectangle {
                        width: Math.max(6, parent.width * share.fraction)
                        height: parent.height
                        radius: 3
                        color: share.selected ? Theme.accentHover : Theme.accent
                        Behavior on width { NumberAnimation { duration: Theme.animNormal } }
                    }
                }
                Text {
                    text: share.detail
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSmall
                }
            }
        }
    }

    component Legend: Row {
        property color swatch
        property string label
        spacing: 6
        Rectangle { width: 10; height: 10; radius: 3; color: parent.swatch; anchors.verticalCenter: parent.verticalCenter }
        Text { text: parent.label; color: Theme.textSecondary; font.pixelSize: Theme.fontSmall }
    }

    component SectionTitle: Text {
        color: Theme.text
        font.pixelSize: Theme.fontBody + 1
        font.weight: Font.DemiBold
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 2 * Theme.spacing
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: column
            x: Math.max(Theme.spacing, (page.width - width) / 2)
            y: Theme.spacing + 4
            width: Math.min(page.width - 2 * Theme.spacing, 980)
            spacing: 12

            // Title and period
            GridLayout {
                columns: page.narrow ? 1 : 2
                columnSpacing: 12
                rowSpacing: 10
                Layout.fillWidth: true
                Text {
                    text: qsTr("Statistics")
                    color: Theme.text
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.Bold
                    Layout.fillWidth: true
                }
                Segmented {
                    options: [{ value: "today", label: qsTr("Today") },
                              { value: "week", label: qsTr("7 days") },
                              { value: "month", label: qsTr("30 days") },
                              { value: "all", label: qsTr("All time") }]
                    value: page.period
                    onActivated: (value) => page.period = value
                    Layout.fillWidth: page.narrow
                }
            }

            Banner {
                visible: !Daemon.statisticsAvailable
                kind: "info"
                title: qsTr("Statistics need a newer agvd")
                text: qsTr("Restart the AGV service after updating: sudo systemctl restart agvd")
                Layout.fillWidth: true
            }

            // Right now: speed, session and the last minute.
            Card {
                Layout.fillWidth: true
                GridLayout {
                    width: parent.width
                    columns: page.wide ? 2 : 1
                    columnSpacing: 24
                    rowSpacing: 14

                    ColumnLayout {
                        spacing: 10
                        Layout.preferredWidth: page.wide && page.liveTraffic ? 260 : -1
                        Layout.fillWidth: !page.wide || !page.liveTraffic

                        RowLayout {
                            spacing: 8
                            Rectangle {
                                width: 8
                                height: 8
                                radius: 4
                                color: Daemon.connected ? Theme.accent : Theme.idle
                            }
                            Text {
                                text: Daemon.connected ? qsTr("Now") : qsTr("Not connected")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                            }
                            Text {
                                visible: Daemon.connected
                                text: page.flag(Daemon.location) + " " + page.locationTitle(Daemon.location)
                                color: Theme.text
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        RowLayout {
                            spacing: 24
                            visible: page.liveTraffic
                            ColumnLayout {
                                spacing: 0
                                Legend { swatch: Theme.chartIn; label: qsTr("Download") }
                                Text {
                                    text: Theme.formatSpeed(page.currentIn)
                                    color: Theme.text
                                    font.pixelSize: Theme.fontHeadline
                                    font.weight: Font.Bold
                                }
                            }
                            ColumnLayout {
                                spacing: 0
                                Legend { swatch: Theme.chartOut; label: qsTr("Upload") }
                                Text {
                                    text: Theme.formatSpeed(page.currentOut)
                                    color: Theme.text
                                    font.pixelSize: Theme.fontHeadline
                                    font.weight: Font.Bold
                                }
                            }
                        }
                        Text {
                            visible: !page.liveTraffic
                            text: Daemon.connected
                                  ? qsTr("Traffic is not measured here in SOCKS5 mode. Connected time still is.")
                                  : qsTr("Connect to see the speed and this session here.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }

                        // The session in progress.
                        Repeater {
                            model: page.stats.sessions.filter(s => s.current)
                            delegate: Text {
                                required property var modelData
                                text: qsTr("This session: %1 · ↓ %2 ↑ %3")
                                      .arg(Theme.formatDuration(modelData.seconds))
                                      .arg(Theme.formatBytes(modelData.in))
                                      .arg(Theme.formatBytes(modelData.out))
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                    }

                    SpeedChart {
                        visible: page.liveTraffic
                        samplesIn: page.speedIn
                        samplesOut: page.speedOut
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.wide ? 120 : 90
                    }
                }
            }

            // Key figures for the period: a row of four only where the long Russian captions fit.
            GridLayout {
                columns: column.width >= 900 ? 4 : 2
                columnSpacing: 12
                rowSpacing: 12
                Layout.fillWidth: true

                Tile {
                    iconName: "shield"
                    caption: qsTr("Through the VPN")
                    value: Theme.formatBytes(page.stats.in + page.stats.out)
                    detail: "↓ %1   ↑ %2".arg(Theme.formatBytes(page.stats.in)).arg(Theme.formatBytes(page.stats.out))
                }
                Tile {
                    iconName: "power"
                    caption: qsTr("Connected")
                    value: Theme.formatDuration(page.stats.seconds)
                    detail: qsTr("%n connection(s)", "", page.stats.connections)
                }
                Tile {
                    iconName: "chart"
                    caption: page.period === "today" ? qsTr("Busiest hour")
                           : page.period === "all" ? qsTr("Busiest month") : qsTr("Busiest day")
                    value: page.stats.peak.bytes ? Theme.formatBytes(page.stats.peak.bytes) : "—"
                    detail: page.stats.peak.tip || qsTr("No traffic yet")
                }
                Tile {
                    iconName: "split"
                    caption: Daemon.appExclusionMode === "vpn-only" ? qsTr("Apps through the VPN") : qsTr("Apps outside the VPN")
                    value: Theme.formatBytes(page.stats.appsIn + page.stats.appsOut)
                    detail: qsTr("%n app(s)", "", page.stats.apps.length)
                }
            }

            // Traffic over time.
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    RowLayout {
                        spacing: 16
                        Layout.fillWidth: true
                        SectionTitle {
                            text: page.selectedApp === "" ? qsTr("Traffic through the VPN")
                                                          : qsTr("Traffic of %1").arg(page.selectedAppName)
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        FlatButton {
                            visible: page.selectedApp !== ""
                            kind: "subtle"
                            compact: true
                            iconName: "close"
                            text: qsTr("All traffic")
                            onClicked: page.selectedApp = ""
                        }
                        Legend { swatch: Theme.chartIn; label: qsTr("Download") }
                        Legend { swatch: Theme.chartOut; label: qsTr("Upload") }
                    }
                    BarChart {
                        bars: page.chartStats.chart
                        maxValue: page.chartStats.chartMax
                        emptyText: qsTr("No traffic in this period yet")
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.narrow ? 170 : 210
                    }
                }
            }

            // Where and what.
            GridLayout {
                columns: page.wide ? 2 : 1
                columnSpacing: 12
                rowSpacing: 12
                Layout.fillWidth: true

                Card {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 14
                        SectionTitle { text: qsTr("Locations") }
                        Repeater {
                            model: page.stats.locations.slice(0, 5)
                            delegate: ShareRow {
                                required property var modelData
                                leading: page.flag(modelData.location) || "🌐"
                                title: page.locationTitle(modelData.location)
                                fraction: page.stats.locations[0].seconds > 0
                                          ? modelData.seconds / page.stats.locations[0].seconds : 0
                                figure: Theme.formatDuration(modelData.seconds)
                                detail: Theme.formatBytes(modelData.in + modelData.out)
                            }
                        }
                        Text {
                            visible: page.stats.locations.length === 0
                            text: qsTr("No connections in this period")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 14
                        SectionTitle {
                            text: Daemon.appExclusionMode === "vpn-only" ? qsTr("Apps through the VPN") : qsTr("Apps outside the VPN")
                        }
                        Repeater {
                            model: page.stats.apps.slice(0, 6)
                            delegate: ShareRow {
                                required property var modelData
                                readonly property real bytes: modelData.in + modelData.out
                                readonly property real topBytes: page.stats.apps[0].in + page.stats.apps[0].out
                                iconSource: "image://appicon/" + encodeURIComponent(modelData.icon || "")
                                title: page.appName(modelData)
                                selected: page.selectedApp === modelData.id
                                clickable: true
                                onClicked: page.selectApp(modelData.id, page.appName(modelData))
                                fraction: topBytes > 0 ? bytes / topBytes : 0
                                figure: Theme.formatBytes(bytes)
                                detail: "↓ %1  ↑ %2".arg(Theme.formatBytes(modelData.in)).arg(Theme.formatBytes(modelData.out))
                            }
                        }
                        Text {
                            visible: page.stats.apps.length > 1 && page.selectedApp === ""
                            text: qsTr("Pick an app to see its traffic on the chart.")
                            color: Theme.textDisabled
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: page.stats.apps.length === 0
                            text: qsTr("Traffic of the apps on the Exclusions page shows up here.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            // Sessions
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width
                    spacing: 4
                    SectionTitle {
                        text: qsTr("Sessions")
                        Layout.bottomMargin: 6
                    }
                    Repeater {
                        model: page.stats.sessions.slice(0, 10)
                        delegate: Rectangle {
                            id: session
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            implicitHeight: 44
                            radius: Theme.radiusSmall
                            color: sessionMouse.containsMouse ? Theme.surfaceHover : "transparent"
                            MouseArea { id: sessionMouse; anchors.fill: parent; hoverEnabled: true }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 12
                                Text {
                                    text: page.flag(session.modelData.location) || "🌐"
                                    font.pixelSize: 18
                                }
                                ColumnLayout {
                                    spacing: 1
                                    Layout.fillWidth: true
                                    Text {
                                        text: page.locationTitle(session.modelData.location)
                                        color: Theme.text
                                        font.pixelSize: Theme.fontBody
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: session.modelData.current ? qsTr("%1 · now").arg(session.modelData.when)
                                                                        : session.modelData.when
                                        color: session.modelData.current ? Theme.accent : Theme.textSecondary
                                        font.pixelSize: Theme.fontSmall
                                    }
                                }
                                Text {
                                    visible: !page.narrow
                                    text: "↓ %1  ↑ %2".arg(Theme.formatBytes(session.modelData.in))
                                                      .arg(Theme.formatBytes(session.modelData.out))
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSmall
                                }
                                Text {
                                    text: Theme.formatDuration(session.modelData.seconds)
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSmall + 1
                                    font.weight: Font.DemiBold
                                    horizontalAlignment: Text.AlignRight
                                    Layout.preferredWidth: 84
                                }
                            }
                        }
                    }
                    Text {
                        visible: page.stats.sessions.length === 0
                        text: qsTr("No connections in this period")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }

            // Where the numbers live, and a way to clear them.
            RowLayout {
                spacing: 12
                Layout.fillWidth: true
                Layout.topMargin: 4
                Text {
                    text: page.stats.since !== ""
                          ? qsTr("Counted since %1. Statistics stay on this computer.").arg(page.stats.since)
                          : qsTr("Statistics stay on this computer.")
                    color: Theme.textDisabled
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                FlatButton {
                    kind: "subtle"
                    compact: true
                    iconName: "download"
                    text: qsTr("Export CSV")
                    enabled: page.stats.hasData
                    onClicked: Daemon.exportStatistics()
                }
                FlatButton {
                    id: resetButton
                    property bool armed: false
                    kind: armed ? "danger" : "subtle"
                    compact: true
                    text: armed ? qsTr("Reset all statistics?") : qsTr("Reset")
                    onClicked: {
                        if (armed) {
                            armed = false
                            Daemon.resetStatistics()
                        } else {
                            armed = true
                            disarm.restart()
                        }
                    }
                    Timer { id: disarm; interval: 4000; onTriggered: resetButton.armed = false }
                }
            }
        }
    }
}
