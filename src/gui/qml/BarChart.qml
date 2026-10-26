import QtQuick
import QtQuick.Layouts
import Agv

// Stacked bars of downloaded (bottom) and uploaded (top) bytes with a light scale and a tooltip
// for the bar under the pointer. bars: [{ label, tip, in, out, showLabel }].
Item {
    id: root

    property var bars: []
    // From the bars themselves, so the scale never lags behind new data for a frame.
    readonly property real maxValue: bars.reduce((top, bar) => Math.max(top, bar.in + bar.out), 0)
    property string emptyText

    // Scale top: a round number of B/KB/MB/GB so the grid labels read well.
    readonly property real scaleMax: {
        let unit = 1
        while (maxValue / unit >= 1024)
            unit *= 1024
        return Theme.niceCeil(Math.max(maxValue, 1) / unit) * unit
    }
    readonly property int labelWidth: 52
    property int hovered: -1

    implicitHeight: 200

    // Grid lines with their values on the left.
    Repeater {
        model: 3
        delegate: Item {
            required property int index
            readonly property real fraction: index / 2
            x: 0
            y: plot.y + plot.height * (1 - fraction)
            width: root.width
            height: 1
            Rectangle {
                x: root.labelWidth
                width: parent.width - root.labelWidth
                height: 1
                color: Theme.chartGrid
            }
            Text {
                visible: root.maxValue > 0 || parent.index === 0
                anchors.verticalCenter: parent.verticalCenter
                width: root.labelWidth - 8
                horizontalAlignment: Text.AlignRight
                text: parent.index === 0 ? "0" : Theme.formatBytes(root.scaleMax * parent.fraction)
                color: Theme.textDisabled
                font.pixelSize: 10
            }
        }
    }

    Item {
        id: plot
        x: root.labelWidth
        y: 8
        width: root.width - root.labelWidth
        height: root.height - 8 - 22

        Row {
            id: row
            anchors.fill: parent
            readonly property real slot: root.bars.length > 0 ? width / root.bars.length : width
            Repeater {
                model: root.bars
                delegate: Item {
                    id: column
                    required property var modelData
                    required property int index
                    readonly property real total: modelData.in + modelData.out
                    width: row.slot
                    height: row.height

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 1
                        anchors.rightMargin: 1
                        radius: 4
                        color: Theme.surfaceHover
                        visible: root.hovered === column.index
                    }
                    // One rounded bar clipped into download and upload parts.
                    Item {
                        id: stack
                        readonly property real barWidth: Math.max(3, Math.min(28, row.slot * 0.62))
                        width: barWidth
                        height: column.total > 0 ? Math.min(row.height, Math.max(2, row.height * column.total / root.scaleMax)) : 0
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        clip: true
                        Behavior on height { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }

                        Rectangle {
                            width: parent.width
                            height: parent.height + radius
                            radius: Math.min(4, parent.width / 2)
                            color: Theme.chartOut
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: column.total > 0 ? parent.height * column.modelData.in / column.total : 0
                            color: Theme.chartIn
                            radius: height >= parent.height - 1 ? Math.min(4, parent.width / 2) : 0
                            // Square bottom corners, rounded only where it is the top of the bar.
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: Math.min(parent.height, 4)
                                color: Theme.chartIn
                            }
                        }
                    }
                    Text {
                        visible: column.modelData.showLabel
                        anchors.top: parent.bottom
                        anchors.topMargin: 6
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: column.modelData.label
                        color: root.hovered === column.index ? Theme.text : Theme.textSecondary
                        font.pixelSize: 10
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: root.hovered = column.index
                        onExited: if (root.hovered === column.index) root.hovered = -1
                    }
                }
            }
        }

        Text {
            anchors.centerIn: parent
            visible: root.maxValue <= 0 && root.emptyText !== ""
            text: root.emptyText
            color: Theme.textDisabled
            font.pixelSize: Theme.fontSmall
        }

        // Tooltip above the hovered bar, kept inside the chart.
        Rectangle {
            id: tip
            readonly property var bar: root.hovered >= 0 ? root.bars[root.hovered] : null
            visible: bar !== null && bar !== undefined
            width: tipColumn.implicitWidth + 20
            height: tipColumn.implicitHeight + 14
            radius: Theme.radiusSmall
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
            x: Math.max(0, Math.min(plot.width - width, row.slot * (root.hovered + 0.5) - width / 2))
            y: -4

            Column {
                id: tipColumn
                anchors.centerIn: parent
                spacing: 3
                Text {
                    text: tip.bar ? tip.bar.tip : ""
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                }
                Row {
                    spacing: 6
                    Rectangle { width: 8; height: 8; radius: 2; color: Theme.chartIn; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        text: qsTr("Download %1").arg(Theme.formatBytes(tip.bar ? tip.bar.in : 0))
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                }
                Row {
                    spacing: 6
                    Rectangle { width: 8; height: 8; radius: 2; color: Theme.chartOut; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        text: qsTr("Upload %1").arg(Theme.formatBytes(tip.bar ? tip.bar.out : 0))
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                    }
                }
            }
        }
    }
}
