import QtQuick
import Agv

// On/off switch in the brand color.
Item {
    id: root

    property bool checked: false
    signal toggled(bool checked)

    implicitWidth: 40
    implicitHeight: 24

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? Theme.accent : Theme.idle
        Behavior on color { ColorAnimation { duration: Theme.animFast } }

        Rectangle {
            width: parent.height - 6
            height: width
            radius: width / 2
            y: 3
            x: root.checked ? parent.width - width - 3 : 3
            color: "#ffffff"
            Behavior on x { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }
}
