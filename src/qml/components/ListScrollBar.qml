// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// Amber's scroll bar beside a list: the thumb shows which part of the whole
// list (loaded or not) is on screen. It takes focus like a list row (Right
// from the list); Up and Down then page the list.
FocusScope {
    id: bar

    // Rows in the whole list, the first one on screen, and how many fit.
    property int total: 0
    property int first: 0
    property int visibleRows: 1

    signal pageUp()
    signal pageDown()
    // Left, OK or Back: back to the list.
    signal leave()
    // Right: whatever lies beyond the bar (the A-Z strip).
    signal next()

    implicitWidth: Theme.px(14)

    Rectangle {
        id: track
        anchors.horizontalCenter: parent.horizontalCenter
        width: bar.activeFocus ? Theme.px(10) : Theme.px(6)
        height: parent.height
        radius: width / 2
        color: bar.activeFocus ? "#40ffffff" : "#20ffffff"
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: track.width
        radius: width / 2
        readonly property real share: bar.total > 0 ? Math.min(1, bar.visibleRows / bar.total) : 1
        height: Math.max(Theme.px(36), bar.height * share)
        y: bar.total > bar.visibleRows
           ? (bar.height - height) * Math.min(1, bar.first / (bar.total - bar.visibleRows)) : 0
        color: bar.activeFocus ? Theme.highlight : "#80ffffff"
    }

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Up:
        case Qt.Key_PageUp:
        case Qt.Key_ChannelUp:
            bar.pageUp()
            break
        case Qt.Key_Down:
        case Qt.Key_PageDown:
        case Qt.Key_ChannelDown:
            bar.pageDown()
            break
        case Qt.Key_Left:
        case Qt.Key_Return:
        case Qt.Key_Back:
            bar.leave()
            break
        case Qt.Key_Right:
            bar.next()
            break
        default:
            return
        }
        event.accepted = true
    }
}
