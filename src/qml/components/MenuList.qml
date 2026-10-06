// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// Amber's vertical text menu. By default the highlight stays at a fixed slot
// and the items scroll under it; staticMode keeps the items still and moves
// the highlight instead. barHighlight draws a solid bar (Amber's submenus)
// rather than coloring the text (the main menu).
ListView {
    id: list

    property bool staticMode: false
    property bool barHighlight: false
    property int pinnedSlot: 3
    property real rowHeight: Theme.px(80)
    property int fontSize: Theme.menuFont
    property string fontFamily: Theme.fontFamily
    property int horizontalAlignment: Text.AlignRight
    property bool dimmed: !activeFocus
    // The role or property holding the label.
    property string textRole: "title"

    signal activated(int index)

    clip: true
    keyNavigationWraps: true
    boundsBehavior: Flickable.StopAtBounds
    highlightMoveDuration: Theme.animation
    highlightRangeMode: staticMode ? ListView.NoHighlightRange : ListView.StrictlyEnforceRange
    preferredHighlightBegin: staticMode ? 0 : pinnedSlot * rowHeight
    preferredHighlightEnd: staticMode ? height : (pinnedSlot + 1) * rowHeight
    opacity: dimmed ? 0.35 : 1
    Behavior on opacity { NumberAnimation { duration: Theme.animation } }

    highlight: Rectangle {
        color: list.barHighlight ? Theme.highlight : "transparent"
        width: list.width
        height: list.rowHeight
    }

    delegate: Item {
        id: row
        required property int index
        required property var modelData
        width: list.width
        height: list.rowHeight

        readonly property bool current: ListView.isCurrentItem
        readonly property string label: typeof modelData === "string" ? modelData : (modelData[list.textRole] || "")

        Text {
            anchors.fill: parent
            anchors.leftMargin: Theme.px(28)
            anchors.rightMargin: Theme.px(28)
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: list.horizontalAlignment
            elide: Text.ElideRight
            text: row.label
            font.family: list.fontFamily
            font.pixelSize: list.fontSize
            color: row.current ? (list.barHighlight ? Theme.highlightText : Theme.highlight) : Theme.dim
        }
    }

    Keys.onReturnPressed: list.activated(list.currentIndex)
}
