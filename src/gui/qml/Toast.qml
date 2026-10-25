import QtQuick
import Agv

// Short message at the bottom of the window; errors stay a bit longer.
Rectangle {
    id: root

    property bool isError: false

    function show(message, error) {
        label.text = message
        isError = error === true
        opacity = 1
        timer.interval = isError ? 6000 : 3500
        timer.restart()
    }

    width: Math.min(parent.width - 2 * Theme.spacing, label.implicitWidth + 2 * Theme.spacing + 30)
    height: label.implicitHeight + 24
    radius: Theme.radius
    color: Theme.dark ? "#2a2a2a" : "#262626"
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: Theme.animNormal } }

    Rectangle {
        width: 4
        height: parent.height - 16
        radius: 2
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        color: root.isError ? Theme.danger : Theme.accent
    }

    Text {
        id: label
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 24
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing
        color: "#f3f3f3"
        font.pixelSize: Theme.fontBody
        wrapMode: Text.Wrap
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.opacity = 0
    }

    Timer {
        id: timer
        onTriggered: root.opacity = 0
    }
}
