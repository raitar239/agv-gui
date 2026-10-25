import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Agv
import Agv.Backend

Item {
    id: page

    property bool embedded: false   // shown beside the home page on wide windows
    signal chosen()

    readonly property var model: Daemon.locations

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        anchors.topMargin: page.embedded ? Theme.spacing : Theme.spacing + 4
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Locations")
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
                Layout.fillWidth: true
            }
            FlatButton {
                kind: "ghost"
                iconName: "refresh"
                compact: true
                enabled: !page.model.loading && Daemon.loggedIn
                onClicked: Daemon.refreshLocations()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Measure ping again")
            }
        }

        InputField {
            iconName: "search"
            placeholderText: qsTr("Search country or city")
            text: page.model.filter
            onTextChanged: page.model.filter = text
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            Segmented {
                options: [{ value: "all", label: qsTr("All") },
                          { value: "favorites", label: qsTr("★ Favorites") }]
                value: page.model.favoritesOnly ? "favorites" : "all"
                onActivated: (value) => page.model.favoritesOnly = value === "favorites"
            }
            Item { Layout.fillWidth: true }
            Segmented {
                options: [{ value: "ping", label: qsTr("Ping") }, { value: "name", label: qsTr("A–Z") }]
                value: page.model.sortByName ? "name" : "ping"
                onActivated: (value) => page.model.sortByName = value === "name"
            }
        }

        ListView {
            id: list
            model: page.model
            clip: true
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            header: LocationRow {
                width: list.width
                visible: page.model.filter === "" && !page.model.favoritesOnly
                height: visible ? implicitHeight : 0
                flag: "⚡"
                country: qsTr("Fastest location")
                city: qsTr("Picked by ping")
                showPing: false
                showFavorite: false
                onClicked: { Daemon.connectFastest(); page.chosen() }
            }

            delegate: LocationRow {
                required property var model

                width: ListView.view.width
                flag: model.flag
                city: model.cityName
                country: model.countryName
                ping: model.ping
                favorite: model.favorite
                // The CLI's own city name is the key for connecting and favorites.
                current: Daemon.connected && model.city.toUpperCase() === Daemon.location.toUpperCase()
                onClicked: { Daemon.connectTo(model.city); page.chosen() }
                onFavoriteToggled: page.model.toggleFavorite(model.city)
            }

            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                width: parent.width - 2 * Theme.spacing
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: page.model.loading ? qsTr("Measuring ping…")
                    : !Daemon.loggedIn ? qsTr("Sign in to see locations")
                    : page.model.favoritesOnly && page.model.filter === ""
                      ? qsTr("No favorites yet. Tap ☆ next to a location to add it.")
                      : qsTr("Nothing found")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontBody
            }
        }
    }
}
