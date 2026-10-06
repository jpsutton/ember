// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// A small square button inside a text field: Paste (a clipboard) or
// Keyboard. The icons are drawn, as Ember bundles no icon font. The field
// handles the keys.
Item {
    id: button

    property string icon  // "paste" or "keyboard"
    // Keyboard: the on-screen keyboard is open.
    property bool checked: false

    readonly property color ink: activeFocus ? Theme.highlightText : checked ? Theme.highlight : Theme.dim
    readonly property real line: Theme.px(3)

    width: Theme.px(56)
    height: Theme.px(56)

    Rectangle {
        anchors.fill: parent
        radius: Theme.px(8)
        color: button.activeFocus ? Theme.highlight : "#2a2c33"
    }

    // A clipboard: a board with a clip at the top and two lines of text.
    Item {
        visible: button.icon === "paste"
        anchors.centerIn: parent
        width: Theme.px(24)
        height: Theme.px(30)
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: Theme.px(3)
            radius: Theme.px(3)
            color: "transparent"
            border.color: button.ink
            border.width: button.line
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.px(12)
            height: Theme.px(7)
            radius: Theme.px(2)
            color: button.ink
        }
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            y: Theme.px(13)
            spacing: Theme.px(4)
            Repeater {
                model: 2
                Rectangle { width: Theme.px(12); height: button.line; color: button.ink }
            }
        }
    }

    // A keyboard: an outline with two rows of keys and a space bar.
    Item {
        visible: button.icon === "keyboard"
        anchors.centerIn: parent
        width: Theme.px(36)
        height: Theme.px(24)
        Rectangle {
            anchors.fill: parent
            radius: Theme.px(3)
            color: "transparent"
            border.color: button.ink
            border.width: button.line
        }
        Column {
            anchors.centerIn: parent
            spacing: Theme.px(3)
            Repeater {
                model: 2
                Row {
                    spacing: Theme.px(3)
                    Repeater {
                        model: 5
                        Rectangle { width: Theme.px(3); height: Theme.px(3); color: button.ink }
                    }
                }
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Theme.px(15)
                height: Theme.px(3)
                color: button.ink
            }
        }
    }
}
