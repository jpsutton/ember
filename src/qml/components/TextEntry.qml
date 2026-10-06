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
                text: root.text === "" ? root.placeholder : (root.password ? "•".repeat(root.text.length) : root.text)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.rowFont
                color: root.text === "" ? Theme.faint : Theme.text
            }
        }

        Keyboard {
            id: keyboard
            focus: true
            onTyped: (text) => root.text += text
            onBackspace: root.text = root.text.slice(0, -1)
            onDone: root.accepted()
        }
    }
}
