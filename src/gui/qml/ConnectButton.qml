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
    signal clicked()

    implicitWidth: diameter
    implicitHeight: diameter

    readonly property color ringColor: working ? Theme.warning : connected ? Theme.accent : Theme.idle

    // Soft halo when connected.
    Rectangle {
        anchors.centerIn: parent
        width: root.diameter
        height: width
        radius: width / 2
        color: Theme.accentSoft
        opacity: root.connected && !root.working ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.animNormal } }
    }

    Rectangle {
        id: face
        anchors.centerIn: parent
        width: root.diameter * 0.78
        height: width
        radius: width / 2
        color: root.connected && !root.working
               ? (mouse.pressed ? Theme.accentPressed : mouse.containsMouse ? Theme.accentHover : Theme.accent)
               : (mouse.pressed ? Theme.surfacePressed : mouse.containsMouse ? Theme.surfaceHover : Theme.surface)
        border.width: root.connected && !root.working ? 0 : Math.max(3, root.diameter * 0.022)
        border.color: root.ringColor
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
        layer.enabled: true
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
