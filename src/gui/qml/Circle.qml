import QtQuick
import QtQuick.Shapes

// A filled, optionally outlined circle drawn as a path. Rectangle with radius = width / 2 is cached
// as a bitmap by the software renderer and gets its far edges cut at fractional scales (1.25, 1.33…).
Shape {
    id: root

    property color color: "transparent"
    property color borderColor: "transparent"
    property real borderWidth: 0

    // Multisampling for smooth edges on the GPU; the software renderer antialiases paths itself.
    layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
    layer.samples: 4

    ShapePath {
        fillColor: root.color
        strokeColor: root.borderWidth > 0 ? root.borderColor : "transparent"
        strokeWidth: root.borderWidth
        PathAngleArc {
            centerX: root.width / 2
            centerY: root.height / 2
            radiusX: (root.width - root.borderWidth) / 2
            radiusY: (root.height - root.borderWidth) / 2
            startAngle: 0
            sweepAngle: 360
        }
    }
}
