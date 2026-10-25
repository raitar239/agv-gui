import QtQuick
import QtQuick.Layouts
import Agv

// Two or three mutually exclusive options in a pill.
Rectangle {
    id: root

    property var options: []      // [{ value, label }]
    property string value
    signal activated(string value)

    color: Theme.dark ? "#262626" : "#ececef"
    radius: Theme.radiusSmall
    implicitHeight: 36
    implicitWidth: row.implicitWidth + 8

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        Repeater {
            model: root.options
            delegate: Rectangle {
                id: option
                required property var modelData
                readonly property bool active: modelData.value === root.value

                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitWidth: label.implicitWidth + 24
                radius: Theme.radiusSmall - 2
                color: active ? Theme.surface : "transparent"
                border.width: active && !Theme.dark ? 1 : 0
                border.color: Theme.border
                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: option.modelData.label
                    color: option.active ? Theme.text : Theme.textSecondary
                    font.pixelSize: Theme.fontSmall + 1
                    font.weight: option.active ? Font.DemiBold : Font.Normal
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (!option.active) root.activated(option.modelData.value)
                }
            }
        }
    }
}
