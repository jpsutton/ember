// SPDX-License-Identifier: GPL-3.0-only
pragma Singleton
import QtQuick

// Sizes are in Amber's 1920x1080 grid: write px(n) for n grid pixels. The
// window sets screenHeight; EmberSettings.uiScale multiplies everything.
QtObject {
    property real screenHeight: 1080
    property real scale: 1.0
    readonly property real u: screenHeight / 1080 * scale

    function px(n) { return Math.round(n * u) }

    readonly property color background: "#0d0e11"
    readonly property color blade: "#e6101116"
    readonly property color bladeEdge: "#30ffffff"
    readonly property color highlight: "#ffb300"
    readonly property color highlightText: "#111111"
    readonly property color text: "#f2f2f2"
    readonly property color dim: "#9a9ca3"
    readonly property color faint: "#5c5f66"
    readonly property color scrim: "#b3000000"
    readonly property color panel: "#cc15161b"
    readonly property color error: "#ff7a6b"

    // Amber's "Default no caps" fontset: Ubuntu Condensed, with Bebas Neue
    // (capitals only) for the home menu.
    readonly property string fontFamily: "Ubuntu Condensed"
    readonly property string menuFontFamily: "Bebas Neue"
    readonly property int menuFont: px(48)
    readonly property int rowFont: px(30)
    readonly property int bodyFont: px(26)
    readonly property int smallFont: px(22)
    readonly property int titleFont: px(48)

    readonly property int animation: 180
}
