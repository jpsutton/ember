// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// One of Amber's media flags, drawn rather than loaded: a tag or an icon on
// a solid block, then the value in an outlined one ("HD | 1080",
// "speaker | DD+"). A badge with only a tag is all solid ("TV-14"), and one
// with only a value all outlined. See jellyfin::InfoBadges for the icons.
Item {
    id: badge

    property string tag
    property string icon
    property string value

    readonly property color ink: "#141519"
    readonly property real border: Math.max(1, Theme.px(2.5))
    readonly property bool hasSolid: tag !== "" || icon !== ""

    height: Theme.px(42)
    width: solid.width + (value !== "" ? valueText.width : 0)

    Rectangle {
        anchors.fill: parent
        radius: Theme.px(4)
        color: "transparent"
        border.color: Theme.text
        border.width: badge.border
    }

    Rectangle {
        id: solid
        visible: badge.hasSolid
        height: parent.height
        width: !badge.hasSolid ? 0
               : badge.icon !== "" && badge.tag === "" ? height
               : (badge.icon !== "" ? height * 0.8 : Theme.px(12)) + tagText.implicitWidth + Theme.px(12)
        radius: Theme.px(4)
        color: Theme.text

        Canvas {
            id: glyph
            visible: badge.icon !== ""
            width: parent.height * 0.7
            height: width
            x: badge.tag === "" ? (parent.width - width) / 2 : parent.height * 0.12
            anchors.verticalCenter: parent.verticalCenter
            onWidthChanged: requestPaint()
            Connections { target: badge; function onIconChanged() { glyph.requestPaint() } }
            // Drawn on a 30 × 30 grid.
            onPaint: {
                const c = getContext("2d")
                c.reset()
                c.scale(width / 30, height / 30)
                c.fillStyle = badge.ink
                c.strokeStyle = badge.ink
                c.lineWidth = 2.5
                const star = (cx, cy, outer, inner) => {
                    c.beginPath()
                    for (let i = 0; i < 10; ++i) {
                        const r = i % 2 ? inner : outer, a = -Math.PI / 2 + i * Math.PI / 5
                        c.lineTo(cx + r * Math.cos(a), cy + r * Math.sin(a))
                    }
                    c.closePath()
                    c.fill()
                }
                switch (badge.icon) {
                case "film":
                    c.fillRect(2, 4, 26, 22)
                    c.clearRect(8, 8, 14, 14)
                    for (let i = 0; i < 4; ++i) { c.clearRect(3.5, 6 + i * 5, 3, 2.5); c.clearRect(23.5, 6 + i * 5, 3, 2.5) }
                    break
                case "speaker":
                    c.beginPath(); c.moveTo(2, 11); c.lineTo(8, 11); c.lineTo(15, 4); c.lineTo(15, 26); c.lineTo(8, 19); c.lineTo(2, 19); c.closePath(); c.fill()
                    c.beginPath(); c.arc(15, 15, 6, -0.9, 0.9); c.stroke()
                    c.beginPath(); c.arc(15, 15, 11, -0.9, 0.9); c.stroke()
                    break
                case "channels":
                    c.strokeRect(5, 5, 20, 20)
                    for (const [x, y] of [[2, 2], [22, 2], [2, 22], [22, 22], [12, 2]]) c.fillRect(x, y, 6, 6)
                    break
                case "aspect":
                    c.strokeRect(2, 7, 26, 16)
                    c.fillRect(6, 11, 4, 2); c.fillRect(6, 11, 2, 4)
                    c.fillRect(20, 17, 4, 2); c.fillRect(22, 15, 2, 4)
                    break
                case "calendar":
                    c.strokeRect(3, 6, 24, 21)
                    c.fillRect(3, 6, 24, 6)
                    c.fillRect(8, 2, 3, 6); c.fillRect(19, 2, 3, 6)
                    for (let row = 0; row < 2; ++row) for (let col = 0; col < 3; ++col) c.fillRect(7 + col * 6, 15 + row * 6, 4, 3)
                    break
                case "clock":
                    c.beginPath(); c.arc(15, 15, 12, 0, 2 * Math.PI); c.stroke()
                    c.beginPath(); c.moveTo(15, 7); c.lineTo(15, 15); c.lineTo(21, 18); c.stroke()
                    break
                case "star":
                    star(15, 16, 14, 6)
                    break
                case "critic":
                    // A tomato: round fruit, a star of leaves cut out of its top.
                    c.beginPath(); c.arc(15, 18, 11, 0, 2 * Math.PI); c.fill()
                    c.fillStyle = Theme.text
                    star(15, 9.5, 8.5, 3)
                    c.fillStyle = badge.ink
                    star(15, 9.5, 5.5, 2)
                    c.fillRect(14, 1, 2.5, 6)
                    break
                case "check":
                    c.lineWidth = 4
                    c.beginPath(); c.moveTo(4, 15); c.lineTo(12, 23); c.lineTo(26, 7); c.stroke()
                    break
                case "heart":
                    c.beginPath()
                    c.moveTo(15, 27)
                    c.bezierCurveTo(2, 18, 0, 10, 4, 6)
                    c.bezierCurveTo(8, 2, 13, 4, 15, 9)
                    c.bezierCurveTo(17, 4, 22, 2, 26, 6)
                    c.bezierCurveTo(30, 10, 28, 18, 15, 27)
                    c.fill()
                    break
                }
            }
        }

        Text {
            id: tagText
            visible: badge.tag !== ""
            x: badge.icon !== "" ? parent.height * 0.8 : Theme.px(6)
            width: parent.width - x - Theme.px(6)
            height: parent.height
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: badge.tag
            font.family: Theme.fontFamily
            font.pixelSize: Theme.px(26)
            font.bold: true
            color: badge.ink
        }
    }

    Text {
        id: valueText
        visible: badge.value !== ""
        x: solid.width
        width: Math.max(implicitWidth + Theme.px(20), badge.hasSolid ? Theme.px(56) : 0)
        height: parent.height
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: badge.value
        font.family: Theme.fontFamily
        font.pixelSize: Theme.px(26)
        color: Theme.text
    }
}
