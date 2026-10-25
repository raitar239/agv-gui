import QtQuick
import QtQuick.Layouts
import Agv

// Bottom navigation for narrow windows (a quarter of the screen on Hyprland, phones).
Rectangle {
    id: root

    property var items: []
    property int currentIndex: 0
    signal selected(int index)

    color: Theme.surface
    implicitHeight: 60

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Repeater {
            model: root.items
            delegate: Item {
                id: item
                required property var modelData
                required property int index
                readonly property bool active: index === root.currentIndex

                Layout.fillWidth: true
                Layout.fillHeight: true

                Rectangle {
                    anchors.centerIn: icon
                    width: 52
                    height: 28
                    radius: 14
                    color: item.active ? Theme.accentSoft : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                }
                AppIcon {
                    id: icon
                    name: item.modelData.icon
                    color: item.active ? Theme.accent : Theme.textSecondary
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 10
                }
                Text {
                    text: item.modelData.short
                    color: item.active ? Theme.accent : Theme.textSecondary
                    font.pixelSize: 11
                    font.weight: item.active ? Font.DemiBold : Font.Normal
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: icon.bottom
                    anchors.topMargin: 6
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.selected(item.index)
                }
            }
        }
    }
}
