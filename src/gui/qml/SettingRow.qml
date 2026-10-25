import QtQuick
import QtQuick.Layouts
import Agv

// A setting with a title, an optional hint and an on/off switch.
RowLayout {
    property alias title: titleText.text
    property alias hint: hintText.text
    property alias checked: toggle.checked
    property bool available: true
    signal toggled(bool checked)
    spacing: 12
    Layout.fillWidth: true
    opacity: available ? 1 : 0.5
    ColumnLayout {
        spacing: 2
        Layout.fillWidth: true
        Text {
            id: titleText
            color: Theme.text
            font.pixelSize: Theme.fontBody
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Text {
            id: hintText
            visible: text !== ""
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
    Toggle {
        id: toggle
        enabled: parent.available
        onToggled: (checked) => parent.toggled(checked)
    }
}
