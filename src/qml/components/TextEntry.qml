// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// A labelled one-line text field that expects a keyboard: a real one, or a
// phone's through KDE Connect. Typing goes straight in, Backspace deletes and
// Enter (or OK) accepts. For the remote, Right moves to two buttons inside
// the field: Paste, which types the clipboard's text (what KDE Connect's
// clipboard sharing sends), and Keyboard, which opens the on-screen keyboard
// below. Back closes that keyboard again. A sensitive field (a password)
// empties the clipboard once it has pasted from it.
FocusScope {
    id: root

    property string label
    property string text
    property bool password: false
    property bool sensitive: password
    property string placeholder
    property bool keyboardShown: false

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
        if (!password) return text
        if (revealLast && text !== "") return "•".repeat(text.length - 1) + text.charAt(text.length - 1)
        return "•".repeat(text.length)
    }

    function type(typed) {
        text += typed
        revealLast = typed.length === 1
        revealTimer.restart()
    }

    function erase() {
        text = text.slice(0, -1)
        revealLast = false
    }

    function paste() {
        const pasted = Clipboard.text()
        if (pasted === "") return
        if (sensitive) Clipboard.clear()
        text += pasted
        revealLast = false
    }

    function showKeyboard(shown) {
        keyboardShown = shown
        if (shown) keyboard.forceActiveFocus()
        else field.forceActiveFocus()
    }

    // Printable text from a key event, for the parts that pass typing on to
    // the field.
    function typedText(event) {
        return event.text.length > 0 && event.text >= " " && !(event.modifiers & Qt.ControlModifier)
               ? event.text : ""
    }

    // Keys any part of the field shares: typing, Backspace and Ctrl+V.
    function handleTyping(event) {
        if (event.key === Qt.Key_Backspace) {
            erase()
        } else if (event.matches(StandardKey.Paste)) {
            paste()
        } else if (typedText(event) !== "") {
            type(typedText(event))
        } else {
            return false
        }
        field.forceActiveFocus()
        event.accepted = true
        return true
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
            border.color: field.activeFocus ? Theme.highlight : Theme.bladeEdge
            border.width: Theme.px(2)

            Item {
                id: field
                anchors.left: parent.left
                anchors.leftMargin: Theme.px(20)
                anchors.right: buttons.left
                anchors.rightMargin: Theme.px(12)
                height: parent.height
                focus: true
                clip: true

                Text {
                    id: shown
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, parent.width - caret.width - Theme.px(4))
                    elide: Text.ElideLeft
                    text: root.text === "" ? root.placeholder : root.displayText()
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.rowFont
                    color: root.text === "" ? Theme.faint : Theme.text
                }
                // The caret, after the text (or before the placeholder).
                Rectangle {
                    id: caret
                    visible: field.activeFocus
                    x: root.text === "" ? 0 : shown.width + Theme.px(2)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.px(3)
                    height: Theme.px(40)
                    color: Theme.highlight
                    SequentialAnimation on opacity {
                        running: caret.visible
                        loops: Animation.Infinite
                        NumberAnimation { to: 1; duration: 0 }
                        PauseAnimation { duration: 550 }
                        NumberAnimation { to: 0; duration: 0 }
                        PauseAnimation { duration: 450 }
                    }
                }

                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Return) {
                        root.accepted()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Right) {
                        pasteButton.forceActiveFocus()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Down && root.keyboardShown) {
                        keyboard.forceActiveFocus()
                        event.accepted = true
                    } else {
                        root.handleTyping(event)
                    }
                }
            }

            Row {
                id: buttons
                anchors.right: parent.right
                anchors.rightMargin: Theme.px(10)
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.px(8)

                FieldButton {
                    id: pasteButton
                    icon: "paste"
                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_Return) {
                            root.paste()
                            field.forceActiveFocus()
                        } else if (event.key === Qt.Key_Left) {
                            field.forceActiveFocus()
                        } else if (event.key === Qt.Key_Right) {
                            keyboardButton.forceActiveFocus()
                        } else if (event.key === Qt.Key_Down && root.keyboardShown) {
                            keyboard.forceActiveFocus()
                        } else {
                            root.handleTyping(event)
                            return
                        }
                        event.accepted = true
                    }
                }
                FieldButton {
                    id: keyboardButton
                    icon: "keyboard"
                    checked: root.keyboardShown
                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_Return) {
                            root.showKeyboard(!root.keyboardShown)
                        } else if (event.key === Qt.Key_Left) {
                            pasteButton.forceActiveFocus()
                        } else if (event.key === Qt.Key_Down && root.keyboardShown) {
                            keyboard.forceActiveFocus()
                        } else {
                            // Right is left to the page (Search moves on to
                            // the results).
                            root.handleTyping(event)
                            return
                        }
                        event.accepted = true
                    }
                }
            }
        }

        Keyboard {
            id: keyboard
            visible: root.keyboardShown
            onTyped: (typed) => root.type(typed)
            onBackspace: root.erase()
            onDone: root.accepted()
        }

        // Back from the on-screen keyboard closes it; otherwise Back goes on
        // to whoever uses the field.
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Back && root.keyboardShown) {
                root.showKeyboard(false)
                event.accepted = true
            }
        }
    }
}
