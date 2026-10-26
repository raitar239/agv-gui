import QtQuick
import QtQuick.Layouts
import Agv

// Side navigation: icons only on medium widths, icons with labels on wide windows.
Rectangle {
    id: root

    property var items: []
    property int currentIndex: 0
    property bool showLabels: false
    signal selected(int index)

    color: Theme.surface
    implicitWidth: showLabels ? 200 : 72

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.spacing
        anchors.leftMargin: root.showLabels ? 12 : 8
        anchors.rightMargin: root.showLabels ? 12 : 8
        spacing: 4

        // Brand mark
        RowLayout {
            spacing: 10
            Layout.alignment: root.showLabels ? Qt.AlignLeft : Qt.AlignHCenter
            Layout.leftMargin: root.showLabels ? 10 : 0
            Layout.bottomMargin: Theme.spacing
            // The app icon in small: the mark on a green tile.
            Rectangle {
                implicitWidth: 30
                implicitHeight: 30
                radius: 9
                gradient: Gradient {
                    GradientStop { position: 0; color: "#83b45f" }
                    GradientStop { position: 1; color: "#4e7838" }
                }
                AppIcon { anchors.centerIn: parent; name: "agv"; color: "white"; size: 24 }
            }
            Text {
                visible: root.showLabels
                text: "AGV"
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.weight: Font.Bold
            }
        }

        Repeater {
            model: root.items
            delegate: Rectangle {
                id: item
                required property var modelData
                required property int index
                readonly property bool active: index === root.currentIndex

                Layout.fillWidth: true
                implicitHeight: root.showLabels ? 44 : 52
                radius: Theme.radiusSmall
                color: active ? Theme.accentSoft : mouse.containsMouse ? Theme.surfaceHover : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                Column {
                    visible: !root.showLabels
                    anchors.centerIn: parent
                    spacing: 2
                    AppIcon {
                        name: item.modelData.icon
                        color: item.active ? Theme.accent : Theme.textSecondary
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    Text {
                        text: item.modelData.short
                        color: item.active ? Theme.accent : Theme.textSecondary
                        font.pixelSize: 10
                        font.weight: item.active ? Font.DemiBold : Font.Normal
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
                Row {
                    visible: root.showLabels
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    spacing: 12
                    AppIcon {
                        name: item.modelData.icon
                        color: item.active ? Theme.accent : Theme.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: item.modelData.label
                        color: item.active ? Theme.text : Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                        font.weight: item.active ? Font.DemiBold : Font.Normal
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.selected(item.index)
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
