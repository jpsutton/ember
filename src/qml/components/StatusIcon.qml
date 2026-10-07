// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// Watched state, as in Amber: a check for watched, a half-filled circle for
// in progress, an empty circle for unwatched. Folders show a count of
// unwatched episodes instead, or nothing. The check is cut out of its disc,
// so it shows the row behind it, highlighted or not.
Item {
    id: root

    property string status
    property int unplayedCount: 0
    property real progress: 0
    property color color: Theme.dim

    implicitWidth: Theme.px(34)
    implicitHeight: Theme.px(34)

    Rectangle {
        anchors.centerIn: parent
        width: Math.max(Theme.px(34), badgeText.implicitWidth + Theme.px(16))
        height: Theme.px(34)
        radius: height / 2
        visible: root.status === "" && root.unplayedCount > 0
        color: "transparent"
        border.color: root.color
        border.width: Math.max(1, Theme.px(2))
        Text {
            id: badgeText
            anchors.centerIn: parent
            text: root.unplayedCount
            color: root.color
            font.family: Theme.fontFamily
            font.pixelSize: Theme.px(20)
            font.bold: true
        }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        visible: root.status !== ""
        property string status: root.status
        property real progress: root.progress
        property color color: root.color
        onStatusChanged: requestPaint()
        onProgressChanged: requestPaint()
        onColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            const r = width / 2 - Math.max(1, Theme.px(2))
            const c = width / 2
            ctx.lineWidth = Math.max(1, Theme.px(2.5))
            ctx.strokeStyle = color
            ctx.fillStyle = color
            ctx.beginPath()
            ctx.arc(c, c, r, 0, 2 * Math.PI)
            if (status === "watched") {
                ctx.fill()
                ctx.globalCompositeOperation = "destination-out"
                ctx.lineWidth = Math.max(1, Theme.px(3.5))
                ctx.beginPath()
                ctx.moveTo(c - r * 0.45, c + r * 0.02)
                ctx.lineTo(c - r * 0.1, c + r * 0.38)
                ctx.lineTo(c + r * 0.5, c - r * 0.35)
                ctx.stroke()
            } else {
                ctx.stroke()
                if (status === "inProgress") {
                    ctx.beginPath()
                    ctx.moveTo(c, c)
                    const p = Math.max(0.12, Math.min(0.95, progress / 100))
                    ctx.arc(c, c, r, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * p)
                    ctx.closePath()
                    ctx.fill()
                }
            }
        }
    }
}
