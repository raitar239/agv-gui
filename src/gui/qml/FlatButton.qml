import QtQuick
import QtQuick.Controls.Basic
import Agv

// Button flavors: "primary" (green), "secondary" (outlined), "ghost" (green text), "subtle" (gray
// text, for row actions), "danger".
Button {
    id: control

    property string kind: "secondary"
    property string iconName
    property bool compact: false

    readonly property color baseColor: {
        switch (kind) {
        case "primary": return Theme.accent
        case "danger": return Theme.danger
        default: return "transparent"
        }
    }
    readonly property color contentColor: {
        if (!enabled)
            return Theme.textDisabled
        switch (kind) {
        case "primary":
        case "danger": return Theme.textOnAccent
        case "ghost": return Theme.accent
        case "subtle": return control.hovered ? Theme.text : Theme.textSecondary
        default: return Theme.text
        }
    }

    implicitHeight: compact ? 32 : Theme.controlHeight
    leftPadding: compact ? 10 : 16
    rightPadding: compact ? 10 : 16
    font.pixelSize: compact ? Theme.fontSmall : Theme.fontBody
    font.weight: Font.DemiBold
    hoverEnabled: true

    contentItem: Row {
        spacing: 6
        AppIcon {
            visible: control.iconName !== ""
            name: control.iconName
            size: control.compact ? 16 : 18
            color: control.contentColor
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: control.text !== ""
            text: control.text
            font: control.font
            color: control.contentColor
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: {
            if (control.kind === "primary" || control.kind === "danger") {
                if (!control.enabled)
                    return Theme.idle
                return control.down ? Qt.darker(control.baseColor, 1.15)
                                    : control.hovered ? Qt.lighter(control.baseColor, 1.08) : control.baseColor
            }
            return control.down ? Theme.surfacePressed : control.hovered ? Theme.surfaceHover : "transparent"
        }
        border.width: control.kind === "secondary" ? 1 : 0
        border.color: Theme.border
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
    }
}
