import QtQuick
import QtQuick.Layouts
import Agv

Rectangle {
    id: root

    property string flag
    property string city
    property string country
    property int ping: -1
    property bool favorite: false
    property bool current: false
    property bool showPing: true
    property bool showFavorite: true
    signal clicked()
    signal favoriteToggled()

    implicitHeight: 56
    radius: Theme.radiusSmall
    color: current ? Theme.accentSoft : mouse.containsMouse ? Theme.surfaceHover : "transparent"
    Behavior on color { ColorAnimation { duration: Theme.animFast } }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 12

        Text {
            text: root.flag
            font.pixelSize: 24
            Layout.preferredWidth: 30
        }
        ColumnLayout {
            spacing: 1
            Layout.fillWidth: true
            Text {
                text: root.country
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: root.current ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: root.city
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        Row {
            visible: root.showPing
            spacing: 6
            Rectangle {
                width: 8
                height: 8
                radius: 4
                color: Theme.pingColor(root.ping)
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: root.ping >= 0 ? qsTr("%1 ms").arg(root.ping) : "—"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Item {
            visible: root.showFavorite
            implicitWidth: 32
            implicitHeight: 32
            AppIcon {
                anchors.centerIn: parent
                name: "star"
                size: 18
                filled: root.favorite
                color: root.favorite ? Theme.warning : (starMouse.containsMouse ? Theme.textSecondary : Theme.textDisabled)
            }
            MouseArea {
                id: starMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.favoriteToggled()
            }
        }
    }
}
