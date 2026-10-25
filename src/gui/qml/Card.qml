import QtQuick
import Agv

Rectangle {
    default property alias content: inner.data
    property int padding: Theme.spacing

    color: Theme.surface
    radius: Theme.radius
    border.width: 1
    border.color: Theme.border
    implicitHeight: inner.childrenRect.height + 2 * padding
    implicitWidth: inner.childrenRect.width + 2 * padding

    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: parent.padding
    }
}
