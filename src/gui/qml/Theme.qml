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

    // Charts: download in the brand green, upload in a lighter tint of it.
    readonly property color chartIn: accent
    readonly property color chartOut: dark ? "#b5d39c" : "#a9cc8c"
    readonly property color chartGrid: dark ? "#2a2a2a" : "#ececec"

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
        const units = [qsTr("B"), qsTr("KB"), qsTr("MB"), qsTr("GB"), qsTr("TB")]
        let value = bytes
        let unit = 0
        while (value >= 1024 && unit < units.length - 1) {
            value /= 1024
            ++unit
        }
        return (unit === 0 || value >= 100 ? Math.round(value) : value.toFixed(1)) + " " + units[unit]
    }

    function formatSpeed(bytesPerSecond) {
        return qsTr("%1/s").arg(formatBytes(bytesPerSecond))
    }

    // 45 s, 12 min, 3 h 05 min, 2 d 4 h
    function formatDuration(seconds) {
        const s = Math.max(0, Math.round(seconds))
        if (s < 60)
            return qsTr("%1 s").arg(s)
        const minutes = Math.floor(s / 60)
        if (minutes < 60)
            return qsTr("%1 min").arg(minutes)
        const hours = Math.floor(minutes / 60)
        if (hours < 24)
            return qsTr("%1 h %2 min").arg(hours).arg(String(minutes % 60).padStart(2, "0"))
        return qsTr("%1 d %2 h").arg(Math.floor(hours / 24)).arg(hours % 24)
    }

    // The smallest round number (1, 2, 2.5, 5 × 10ⁿ) at or above value, for chart scales.
    function niceCeil(value) {
        if (value <= 0)
            return 1
        const power = Math.pow(10, Math.floor(Math.log10(value)))
        for (const step of [1, 2, 2.5, 5, 10]) {
            if (step * power >= value)
                return step * power
        }
        return 10 * power
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
