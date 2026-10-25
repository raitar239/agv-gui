import QtQuick
import QtQuick.Controls.Basic
import Agv

// A themed drop-down over [{ value, label }] options.
ComboBox {
    id: control

    property var options: []
    property string value
    signal activatedValue(string value)

    model: options
    textRole: "label"
    valueRole: "value"
    // Looked up in options directly: indexOfValue() runs before the model is populated.
    currentIndex: {
        for (let i = 0; i < options.length; ++i) {
            if (options[i].value === value)
                return i
        }
        return -1
    }
    onActivated: (index) => activatedValue(options[index].value)

    implicitWidth: 180
    implicitHeight: 36
    leftPadding: 12
    rightPadding: 32
    font.pixelSize: Theme.fontBody
    hoverEnabled: true

    contentItem: Text {
        text: control.displayText
        color: Theme.text
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: AppIcon {
        name: "chevron"
        size: 16
        color: Theme.textSecondary
        rotation: control.popup.visible ? -90 : 90
        x: control.width - width - 10
        y: (control.height - height) / 2
        Behavior on rotation { NumberAnimation { duration: Theme.animFast } }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.hovered ? Theme.surfaceHover : Theme.surface
        border.width: control.visualFocus || control.popup.visible ? 2 : 1
        border.color: control.visualFocus || control.popup.visible ? Theme.accent : Theme.border
    }

    delegate: ItemDelegate {
        id: item
        required property var modelData
        required property int index
        width: ListView.view.width
        height: 36
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: item.modelData.label
            color: item.index === control.currentIndex ? Theme.accent : Theme.text
            font.pixelSize: Theme.fontBody
            font.weight: item.index === control.currentIndex ? Font.DemiBold : Font.Normal
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.radiusSmall - 2
            color: item.highlighted ? Theme.surfaceHover : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        padding: 4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
        }

        background: Rectangle {
            radius: Theme.radiusSmall
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
        }
    }
}
