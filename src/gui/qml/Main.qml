import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Agv
import Agv.Backend

ApplicationWindow {
    id: window

    property int currentPage: 0
    property string screenshotState  // development: set by --screenshot

    // Breakpoints by width; heights below 480 (a horizontal half on a laptop) tighten spacing.
    readonly property string layoutMode: width < 520 ? "compact" : width < 900 ? "medium" : "expanded"
    readonly property bool shortHeight: height < 480

    readonly property var pages: [
        { icon: "home", label: qsTr("Home"), short: qsTr("Home") },
        { icon: "globe", label: qsTr("Locations"), short: qsTr("Locations") },
        { icon: "split", label: qsTr("Exclusions"), short: qsTr("Exclusions") },
        { icon: "settings", label: qsTr("Settings"), short: qsTr("Settings") }
    ]

    width: 960
    height: 640
    minimumWidth: 320
    minimumHeight: 360
    title: Daemon.connected ? qsTr("AdGuard VPN — Protected") : qsTr("AdGuard VPN")
    color: Theme.bg

    palette.window: Theme.bg
    palette.base: Theme.surface
    palette.text: Theme.text
    palette.windowText: Theme.text
    palette.buttonText: Theme.text
    palette.button: Theme.surface
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.textOnAccent
    palette.placeholderText: Theme.textDisabled
    palette.toolTipBase: Theme.surface
    palette.toolTipText: Theme.text

    Connections {
        target: Daemon
        function onErrorOccurred(message) { toast.show(message, true) }
        function onInfo(message) { toast.show(message, false) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            spacing: 0
            Layout.fillWidth: true
            Layout.fillHeight: true

            NavRail {
                visible: window.layoutMode !== "compact"
                showLabels: window.layoutMode === "expanded"
                items: window.pages
                currentIndex: window.currentPage
                onSelected: (index) => window.currentPage = index
                Layout.fillHeight: true
            }

            StackLayout {
                currentIndex: window.currentPage
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Home; on wide windows the location list sits beside it.
                RowLayout {
                    spacing: 0
                    HomePage {
                        compactHeight: window.shortHeight
                        showLocationButton: window.layoutMode !== "expanded"
                        onOpenLocations: window.currentPage = 1
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }
                    Rectangle {
                        visible: window.layoutMode === "expanded"
                        width: 1
                        color: Theme.border
                        Layout.fillHeight: true
                    }
                    LocationsPage {
                        visible: window.layoutMode === "expanded"
                        embedded: true
                        Layout.preferredWidth: Math.min(420, window.width * 0.42)
                        Layout.fillHeight: true
                    }
                }
                LocationsPage {
                    onChosen: if (window.layoutMode !== "expanded") window.currentPage = 0
                }
                ExclusionsPage {}
                SettingsPage {}
            }
        }

        BottomBar {
            visible: window.layoutMode === "compact"
            items: window.pages
            currentIndex: window.currentPage
            onSelected: (index) => window.currentPage = index
            Layout.fillWidth: true
        }
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: window.layoutMode === "compact" ? 72 : Theme.spacing
    }
}
