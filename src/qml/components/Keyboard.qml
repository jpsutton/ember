// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// An on-screen keyboard for the remote. Arrows move, OK types. Emits
// typed(text), backspace() and done(). A physical keyboard also works:
// printable keys type directly.
FocusScope {
    id: root

    property bool shifted: false
    property bool symbols: false
    // Set while text arrives from a real keyboard (or a phone's, through
    // KDE Connect): Enter then means "done", not "press the highlighted key".
    // Moving with the arrows hands control back to the on-screen keys.
    property bool typing: false

    signal typed(string text)
    signal backspace()
    signal done()

    readonly property var letterRows: [
        "1234567890".split(""),
        "qwertyuiop".split(""),
        "asdfghjkl-".split(""),
        "zxcvbnm._@".split(""),
    ]
    readonly property var symbolRows: [
        "1234567890".split(""),
        "!#$%&*()+=".split(""),
        "/:;'\"?,<>~".split(""),
        "[]{}|\\^`_@".split(""),
    ]
    readonly property var actionRow: ["⇧", "?123", "space", "⌫", "done"]
    readonly property int columns: 10
    property int row: 1
    property int column: 0

    implicitWidth: columns * Theme.px(86)
    implicitHeight: 5 * Theme.px(82)

    function rows() { return symbols ? symbolRows : letterRows }

    function keyAt(r, c) {
        if (r < 4) {
            const key = rows()[r][c]
            return shifted && !symbols ? key.toUpperCase() : key
        }
        return actionRow[c]
    }

    function columnCount(r) { return r < 4 ? columns : actionRow.length }

    function press() {
        const key = keyAt(row, column)
        if (row < 4) {
            typed(key)
            if (shifted) shifted = false
        } else if (key === "⇧") {
            shifted = !shifted
        } else if (key === "?123") {
            symbols = !symbols
        } else if (key === "space") {
            typed(" ")
        } else if (key === "⌫") {
            backspace()
        } else {
            done()
        }
    }

    Column {
        anchors.fill: parent
        spacing: Theme.px(8)
        Repeater {
            model: 5
            Row {
                id: keyRow
                required property int index
                spacing: Theme.px(8)
                readonly property int count: root.columnCount(index)
                Repeater {
                    model: keyRow.count
                    Rectangle {
                        required property int index
                        readonly property bool current: root.activeFocus && root.row === keyRow.index && root.column === index
                        width: keyRow.count === root.columns ? Theme.px(78)
                               : (root.columns * Theme.px(86) - (keyRow.count - 1) * Theme.px(8)) / keyRow.count
                        height: Theme.px(74)
                        radius: Theme.px(8)
                        color: current ? Theme.highlight : "#2a2c33"
                        border.color: (keyRow.index === 4 && index === 0 && root.shifted)
                                      || (keyRow.index === 4 && index === 1 && root.symbols) ? Theme.highlight : "transparent"
                        border.width: Theme.px(2)
                        Text {
                            anchors.centerIn: parent
                            text: root.keyAt(keyRow.index, index)
                            font.family: Theme.fontFamily
                            font.pixelSize: keyRow.index === 4 ? Theme.smallFont : Theme.rowFont
                            font.bold: parent.current
                            color: parent.current ? Theme.highlightText : Theme.text
                        }
                    }
                }
            }
        }
    }

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Up:
            typing = false
            row = (row + 4) % 5
            column = Math.min(column, columnCount(row) - 1)
            break
        case Qt.Key_Down:
            typing = false
            row = (row + 1) % 5
            column = Math.min(column, columnCount(row) - 1)
            break
        case Qt.Key_Left:
            typing = false
            column = (column + columnCount(row) - 1) % columnCount(row)
            break
        case Qt.Key_Right:
            typing = false
            column = (column + 1) % columnCount(row)
            break
        case Qt.Key_Return:
            if (typing) done()
            else press()
            break
        case Qt.Key_Backspace:
            backspace()
            break
        default:
            if (event.text.length > 0 && event.text >= " " && !(event.modifiers & Qt.ControlModifier)) {
                typing = true
                typed(event.text)
                break
            }
            return
        }
        event.accepted = true
    }
}
