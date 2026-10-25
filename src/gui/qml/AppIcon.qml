import QtQuick
import QtQuick.Shapes
import Agv

// Line icons drawn on a 24×24 grid, so no image files or SVG module are needed.
Item {
    id: root

    property string name
    property color color: Theme.text
    property real size: 20
    property bool filled: false

    readonly property var paths: ({
        "home": "M4 10.5 L12 4 L20 10.5 M6 9 V20 H18 V9 M10 20 V14 H14 V20",
        "globe": "M3 12 A9 9 0 1 0 21 12 A9 9 0 1 0 3 12 M3 12 H21 M12 3 C8.4 6.6 8.4 17.4 12 21 M12 3 C15.6 6.6 15.6 17.4 12 21",
        "split": "M12 21 V14 M12 14 C12 10 6 10 6 6 V3 M12 14 C12 10 18 10 18 6 V3 M3.5 5.5 L6 3 L8.5 5.5 M15.5 5.5 L18 3 L20.5 5.5",
        "settings": "M4 7 H13 M17 7 H20 M15 5 V9 M4 17 H7 M11 17 H20 M9 15 V19",
        "user": "M8 8 A4 4 0 1 0 16 8 A4 4 0 1 0 8 8 M4 20.5 C4 16.5 7.6 14.5 12 14.5 C16.4 14.5 20 16.5 20 20.5",
        "power": "M12 3.5 V11.5 M7.2 6.3 A7.6 7.6 0 1 0 16.8 6.3",
        "search": "M4.5 10.5 A6 6 0 1 0 16.5 10.5 A6 6 0 1 0 4.5 10.5 M15 15 L20 20",
        "plus": "M12 5 V19 M5 12 H19",
        "close": "M6.5 6.5 L17.5 17.5 M17.5 6.5 L6.5 17.5",
        "star": "M12 3.8 L14.5 9 L20.2 9.6 L15.9 13.4 L17.1 19 L12 16.1 L6.9 19 L8.1 13.4 L3.8 9.6 L9.5 9 Z",
        "bolt": "M13.5 3 L5.5 13.5 H11.5 L10.5 21 L18.5 10.5 H12.5 Z",
        "chevron": "M9 5.5 L15.5 12 L9 18.5",
        "sort": "M7 4.5 V19.5 M4 16.5 L7 19.5 L10 16.5 M17 19.5 V4.5 M14 7.5 L17 4.5 L20 7.5",
        "refresh": "M19.5 12 A7.5 7.5 0 1 1 17.3 6.7 M19.5 4 V8.5 H15",
        "logout": "M14 4 H19 V20 H14 M10 8 L6 12 L10 16 M6 12 H15.5",
        "login": "M14 4 H19 V20 H14 M9 8 L13 12 L9 16 M3.5 12 H13",
        "warning": "M12 4 L21 19.5 H3 Z M12 10 V14 M12 16.8 V17",
        "check": "M5 12.5 L10 17 L19 7",
        "shield": "M12 3 L19 6 V11 C19 15.5 16 19 12 21 C8 19 5 15.5 5 11 V6 Z",
        "download": "M12 4 V15 M7.5 10.5 L12 15 L16.5 10.5 M5 19.5 H19",
        "palette": "M12 3.5 A8.5 8.5 0 1 0 12 20.5 C13.5 20.5 13.5 18.5 12.8 17.6 C12 16.6 12.6 15 14 15 H16.5 C18.7 15 20.5 13.2 20.5 11 C20.5 6.9 16.7 3.5 12 3.5 M7.5 11 V11.2 M10 7.5 V7.7 M14.5 7.5 V7.7"
    })

    implicitWidth: size
    implicitHeight: size

    Shape {
        anchors.fill: parent
        // Multisampling for smooth edges on the GPU. The software renderer antialiases paths
        // itself and draws layers wrong at fractional scales (1.25, 1.33…), so no layer there.
        layer.enabled: GraphicsInfo.api !== GraphicsInfo.Software
        layer.samples: 4

        ShapePath {
            scale: Qt.size(root.size / 24, root.size / 24)
            strokeColor: root.color
            strokeWidth: 1.8 * root.size / 24
            fillColor: root.filled ? root.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.paths[root.name] || "" }
        }
    }
}
