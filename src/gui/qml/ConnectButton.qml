import QtQuick
import QtQuick.Shapes
import Agv

// The big round switch. Green and filled when protected, a spinning arc while changing.
Item {
    id: root

    property bool connected: false
    property bool working: false
    property bool enabledState: true
    property real diameter: 180
    // Whole, even pixel sizes: the software renderer clips circles with fractional sizes.
    readonly property int outer: 2 * Math.round(diameter / 2)
    readonly property int inner: 2 * Math.round(diameter * 0.39)
    signal clicked()

    implicitWidth: outer
    implicitHeight: outer

    readonly property color ringColor: working ? Theme.warning : connected ? Theme.accent : Theme.idle

    // Soft halo when connected.
    Circle {
        anchors.centerIn: parent
        width: root.outer
        height: width
        color: Theme.accentSoft
        opacity: root.connected && !root.working ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.animNormal } }
    }

    Circle {
        id: face
        anchors.centerIn: parent
        width: root.inner
        height: width
        color: root.connected && !root.working
               ? (mouse.pressed ? Theme.accentPressed : mouse.containsMouse ? Theme.accentHover : Theme.accent)
               : (mouse.pressed ? Theme.surfacePressed : mouse.containsMouse ? Theme.surfaceHover : Theme.surface)
        borderWidth: root.connected && !root.working ? 0 : Math.max(3, root.diameter * 0.022)
        borderColor: root.ringColor
        scale: mouse.pressed ? 0.97 : 1
        Behavior on color { ColorAnimation { duration: Theme.animNormal } }
        Behavior on scale { NumberAnimation { duration: Theme.animFast } }

        AppIcon {
            anchors.centerIn: parent
            name: "power"
            size: face.width * 0.36
            color: root.connected && !root.working ? Theme.textOnAccent
                                                   : root.enabledState ? Theme.text : Theme.textDisabled
        }
    }

    // Progress arc while connecting or disconnecting.
    Shape {
        anchors.centerIn: parent
        width: face.width
        height: face.height
        visible: root.working
        // Multisampling for smooth edges on the GPU. The software renderer antialiases paths
        // itself and draws layers wrong at fractional scales (1.25, 1.33…), so no layer there.
        layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
        layer.samples: 4

        ShapePath {
            strokeColor: Theme.warning
            strokeWidth: Math.max(3, root.diameter * 0.022)
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: face.width / 2
                centerY: face.height / 2
                radiusX: face.width / 2 - 2
                radiusY: face.height / 2 - 2
                startAngle: -90
                sweepAngle: 90
            }
        }
        RotationAnimator on rotation {
            running: root.working
            from: 0
            to: 360
            duration: 1100
            loops: Animation.Infinite
        }
    }

    MouseArea {
        id: mouse
        anchors.centerIn: parent
        width: face.width
        height: face.height
        hoverEnabled: true
        enabled: root.enabledState && !root.working
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }
}
