import QtQuick
import QtQuick.Controls.Basic
import Agv

TextField {
    id: control

    property string iconName

    implicitHeight: Theme.controlHeight
    leftPadding: iconName !== "" ? 38 : 12
    rightPadding: 12
    color: Theme.text
    placeholderTextColor: Theme.textDisabled
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    font.pixelSize: Theme.fontBody
    verticalAlignment: TextInput.AlignVCenter

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.surface
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent : Theme.border

        AppIcon {
            visible: control.iconName !== ""
            name: control.iconName
            size: 18
            color: Theme.textSecondary
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 12
        }
    }
}
