// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// A labelled text box with the on-screen keyboard under it. Emits accepted()
// when the keyboard's Done key is pressed.
FocusScope {
    id: root

    property string label
    property string text
    property bool password: false
    property string placeholder

    signal accepted()

    // In a password, the last character shows for a moment after it is
    // typed, so a slip is visible.
    property bool revealLast: false
    Timer {
        id: revealTimer
        interval: 1500
        onTriggered: root.revealLast = false
    }

    function displayText() {
        if (text === "") return placeholder
        if (!password) return text
        if (revealLast) return "•".repeat(text.length - 1) + text.charAt(text.length - 1)
        return "•".repeat(text.length)
    }

    implicitWidth: keyboard.implicitWidth
    implicitHeight: column.implicitHeight

    Column {
        id: column
        width: parent.width
        spacing: Theme.px(18)

        Text {
            text: root.label
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: Theme.dim
        }

        Rectangle {
            width: parent.width
            height: Theme.px(76)
            radius: Theme.px(8)
            color: "#1d1f25"
            border.color: root.activeFocus ? Theme.highlight : Theme.bladeEdge
            border.width: Theme.px(2)
            Text {
                anchors.fill: parent
                anchors.leftMargin: Theme.px(20)
                anchors.rightMargin: Theme.px(20)
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideLeft
                text: root.displayText()
                font.family: Theme.fontFamily
                font.pixelSize: Theme.rowFont
                color: root.text === "" ? Theme.faint : Theme.text
            }
        }

        Keyboard {
            id: keyboard
            focus: true
            onTyped: (text) => {
                root.text += text
                root.revealLast = true
                revealTimer.restart()
            }
            onBackspace: {
                root.text = root.text.slice(0, -1)
                root.revealLast = false
            }
            onDone: root.accepted()
        }
    }
}
