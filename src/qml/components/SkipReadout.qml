// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// The skip readout over the picture: the run of skips so far ("+30s",
// "-1:20") at the side the skip goes, with a chevron drifting that way. No
// backing, so it covers as little of the picture as it can (Plezy's
// design). Set anchors.verticalCenter; it places itself by x, since anchors
// swapped from one side to the other stay put.
Row {
    id: root

    property real total
    property bool forward: true
    property bool shown: false

    x: forward ? parent.width - width - Theme.px(96) : Theme.px(96)
    // The chevron sits on the outer side.
    layoutDirection: forward ? Qt.LeftToRight : Qt.RightToLeft
    spacing: Theme.px(12)
    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 300 } }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        readonly property int seconds: Math.round(root.total)
        text: (root.forward ? "+" : "−") + (seconds < 60 ? seconds + "s" : Format.clock(seconds))
        font.family: Theme.fontFamily
        font.pixelSize: Theme.px(56)
        font.bold: true
        color: "white"
        style: Text.Outline
        styleColor: "#a0000000"
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: root.forward ? "›" : "‹"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.px(84)
        font.bold: true
        color: "white"
        style: Text.Outline
        styleColor: "#a0000000"
        transform: Translate { id: drift }
        SequentialAnimation {
            running: root.visible
            loops: Animation.Infinite
            NumberAnimation {
                target: drift; property: "x"; from: 0; to: (root.forward ? 1 : -1) * Theme.px(14)
                duration: 770; easing.type: Easing.OutQuad
            }
            NumberAnimation {
                target: drift; property: "x"; to: 0
                duration: 330; easing.type: Easing.InOutQuad
            }
        }
    }
}
