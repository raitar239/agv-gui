import QtQuick
import QtQuick.Layouts
import Agv

// A one-line notice with an optional action: "warning", "danger" or "info".
Rectangle {
    id: root

    property string kind: "warning"
    property string title
    property string text
    property string actionText
    signal action()

    readonly property color tone: kind === "danger" ? Theme.danger : kind === "info" ? Theme.accent : Theme.warning

    color: kind === "danger" ? Theme.dangerSoft : kind === "info" ? Theme.accentSoft : Theme.warningSoft
    radius: Theme.radius
    border.width: 1
    border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.35)
    implicitHeight: layout.implicitHeight + 2 * 12

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        AppIcon {
            name: root.kind === "info" ? "shield" : "warning"
            color: root.tone
            Layout.alignment: Qt.AlignTop
        }
        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Text {
                visible: root.title !== ""
                text: root.title
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Text {
                visible: root.text !== ""
                text: root.text
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        FlatButton {
            visible: root.actionText !== ""
            text: root.actionText
            kind: "secondary"
            compact: true
            onClicked: root.action()
        }
    }
}
