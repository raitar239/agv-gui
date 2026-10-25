pragma Singleton
import QtQuick
import Agv.Backend

// Design tokens. Colors are AdGuard VPN's own (adguard-vpn.com stylesheet: --vpngreen400 #74a352,
// --vpngreen700 #568040, --green-0/-5, --black #131313, --black900 #1f1f1f, --gray*, --red-50,
// --orange-50), mapped onto light and dark surfaces.
QtObject {
    readonly property bool dark: SystemTheme.dark

    // Brand
    readonly property color accent: "#74a352"
    readonly property color accentHover: dark ? "#86b266" : "#689249"
    readonly property color accentPressed: "#568040"
    readonly property color accentSoft: dark ? Qt.rgba(0.455, 0.639, 0.322, 0.16) : "#f2f6ee"
    readonly property color accentSoftBorder: dark ? Qt.rgba(0.455, 0.639, 0.322, 0.35) : "#dfead6"
    readonly property color textOnAccent: "#ffffff"

    // Surfaces
    readonly property color bg: dark ? "#131313" : "#f6f6f6"
    readonly property color surface: dark ? "#1f1f1f" : "#ffffff"
    readonly property color surfaceHover: dark ? "#292929" : "#f3f3f3"
    readonly property color surfacePressed: dark ? "#333333" : "#e9e9ed"
    readonly property color border: dark ? "#2e2e2e" : "#e4e4e4"
    readonly property color idle: dark ? "#3d3d3d" : "#d8d8d8"

    // Text
    readonly property color text: dark ? "#f3f3f3" : "#131313"
    readonly property color textSecondary: dark ? "#a4a4a4" : "#5e5e5e"
    readonly property color textDisabled: dark ? "#5e5e5e" : "#a4a4a4"

    // Status
    readonly property color danger: "#f44725"
    readonly property color dangerSoft: dark ? Qt.rgba(0.957, 0.278, 0.145, 0.15) : "#fef6f5"
    readonly property color warning: "#ff9500"
    readonly property color warningSoft: dark ? Qt.rgba(1, 0.584, 0, 0.14) : "#fff9f0"

    // Shape and rhythm
    readonly property int radius: 12
    readonly property int radiusSmall: 8
    readonly property int spacing: 16
    readonly property int spacingSmall: 8
    readonly property int controlHeight: 40

    readonly property int fontSmall: 12
    readonly property int fontBody: 14
    readonly property int fontTitle: 18
    readonly property int fontHeadline: 24

    readonly property int animFast: 150
    readonly property int animNormal: 200

    function formatBytes(bytes) {
        const units = ["B", "KB", "MB", "GB", "TB"]
        let value = bytes
        let unit = 0
        while (value >= 1024 && unit < units.length - 1) {
            value /= 1024
            ++unit
        }
        return (unit === 0 || value >= 100 ? Math.round(value) : value.toFixed(1)) + " " + units[unit]
    }

    function pingColor(ping) {
        if (ping < 0)
            return textDisabled
        if (ping < 80)
            return accent
        if (ping < 160)
            return warning
        return danger
    }
}
