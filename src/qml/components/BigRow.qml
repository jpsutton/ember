// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// Amber's Big List row: a small poster, the title, and a line of details.
Item {
    id: row

    property var item: ({})
    property string title
    property bool current: false

    implicitHeight: Theme.px(110)

    Rectangle {
        anchors.fill: parent
        color: Theme.highlight
        visible: row.current
    }

    Image {
        id: thumb
        x: Theme.px(14)
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.px(60)
        height: Theme.px(90)
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        source: row.item.poster || ""
        sourceSize.height: Theme.px(90)
    }

    Column {
        anchors.left: thumb.right
        anchors.leftMargin: Theme.px(16)
        anchors.right: icon.left
        anchors.rightMargin: Theme.px(12)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.px(4)
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: row.title
            font.family: Theme.fontFamily
            font.capitalization: Theme.caps
            font.pixelSize: Theme.rowFont
            font.bold: row.current
            color: row.current ? Theme.highlightText : Theme.text
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: [row.item.year, row.item.runtimeText, row.item.communityRating ? "★ " + row.item.communityRating : "",
                   row.item.officialRating].filter(s => s).join("  ·  ")
            font.family: Theme.fontFamily
            font.capitalization: Theme.caps
            font.pixelSize: Theme.smallFont
            color: row.current ? Theme.highlightText : Theme.dim
        }
    }

    StatusIcon {
        id: icon
        anchors.right: parent.right
        anchors.rightMargin: Theme.px(20)
        anchors.verticalCenter: parent.verticalCenter
        status: row.item.status || ""
        unplayedCount: row.item.unplayedCount || 0
        progress: row.item.playedPercentage || 0
        color: row.current ? Theme.highlightText : Theme.dim
    }
}
