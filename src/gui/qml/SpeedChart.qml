import QtQuick
import Agv

// The last minute of tunnel speed: download as a filled area, upload as a line.
// samplesIn/samplesOut are bytes per second, oldest first; count is how many fit the width.
Item {
    id: root

    property var samplesIn: []
    property var samplesOut: []
    property int count: 60

    readonly property real peak: Math.max(1, Math.max.apply(null, samplesIn.concat(samplesOut, [0])))
    readonly property real scaleMax: {
        let unit = 1
        while (peak / unit >= 1024)
            unit *= 1024
        return Theme.niceCeil(peak / unit) * unit
    }

    implicitHeight: 120
    onSamplesInChanged: canvas.requestPaint()
    onScaleMaxChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()

    Connections {
        target: Theme
        function onDarkChanged() { canvas.requestPaint() }
    }

    Rectangle { width: parent.width; height: 1; color: Theme.chartGrid }
    Rectangle { y: parent.height / 2; width: parent.width; height: 1; color: Theme.chartGrid }
    Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: Theme.chartGrid }
    Text {
        anchors.right: parent.right
        y: 3
        text: Theme.formatSpeed(root.scaleMax)
        color: Theme.textDisabled
        font.pixelSize: 10
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        renderStrategy: Canvas.Immediate

        function points(samples) {
            const step = width / Math.max(1, root.count - 1)
            const offset = root.count - samples.length
            const result = []
            for (let i = 0; i < samples.length; ++i)
                result.push([(offset + i) * step, height - 1 - (height - 2) * samples[i] / root.scaleMax])
            return result
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const down = points(root.samplesIn)
            const up = points(root.samplesOut)
            if (down.length < 2)
                return

            // Download: area with a soft gradient under a solid line.
            const gradient = ctx.createLinearGradient(0, 0, 0, height)
            gradient.addColorStop(0, Qt.rgba(Theme.chartIn.r, Theme.chartIn.g, Theme.chartIn.b, 0.35))
            gradient.addColorStop(1, Qt.rgba(Theme.chartIn.r, Theme.chartIn.g, Theme.chartIn.b, 0.02))
            ctx.beginPath()
            ctx.moveTo(down[0][0], height)
            for (const p of down)
                ctx.lineTo(p[0], p[1])
            ctx.lineTo(down[down.length - 1][0], height)
            ctx.closePath()
            ctx.fillStyle = gradient
            ctx.fill()

            ctx.lineJoin = "round"
            ctx.lineWidth = 2
            ctx.strokeStyle = Theme.chartIn
            ctx.beginPath()
            down.forEach((p, i) => i === 0 ? ctx.moveTo(p[0], p[1]) : ctx.lineTo(p[0], p[1]))
            ctx.stroke()

            ctx.lineWidth = 1.5
            ctx.strokeStyle = Theme.chartOut
            ctx.beginPath()
            up.forEach((p, i) => i === 0 ? ctx.moveTo(p[0], p[1]) : ctx.lineTo(p[0], p[1]))
            ctx.stroke()
        }
    }
}
