// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// One row of an item list: title, a grey second label that depends on the
// sort, and the watched state on the right.
Item {
    id: row

    property string title
    property string label2
    property string status
    property int unplayedCount: 0
    property real progress: 0
    property bool current: false

    implicitHeight: Theme.px(72)

    Rectangle {
        anchors.fill: parent
        color: Theme.highlight
        visible: row.current
    }

    Text {
        id: titleText
        anchors.left: parent.left
        anchors.leftMargin: Theme.px(24)
        anchors.right: label.left
        anchors.rightMargin: Theme.px(16)
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        text: row.title
        font.family: Theme.fontFamily
        font.pixelSize: Theme.rowFont
        font.bold: row.current
        color: row.current ? Theme.highlightText : Theme.text
    }

    Text {
        id: label
        anchors.right: icon.left
        anchors.rightMargin: Theme.px(16)
        anchors.verticalCenter: parent.verticalCenter
        text: row.label2
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallFont
        color: row.current ? Theme.highlightText : Theme.dim
    }

    StatusIcon {
        id: icon
        anchors.right: parent.right
        anchors.rightMargin: Theme.px(20)
        anchors.verticalCenter: parent.verticalCenter
        status: row.status
        unplayedCount: row.unplayedCount
        progress: row.progress
        color: row.current ? Theme.highlightText : Theme.dim
    }
}
