// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// A modal list of choices: context menus, the side blade, track pickers.
// open() takes a title, options ({title, detail, checked}) and a callback
// that receives the chosen option's index. Back closes it without a choice.
FocusScope {
    id: root

    property string title
    property var options: []
    property var callback: null
    property string dock: "center"   // "center" or "left" (the side blade)
    property Item returnFocus: null

    signal closed()

    function open(title, options, callback, dock, startIndex) {
        root.title = title
        root.options = options
        // Set imperatively: a list rebound while hidden kept its old rows.
        list.model = []
        list.model = options
        root.callback = callback
        root.dock = dock || "center"
        returnFocus = root.Window.activeFocusItem
        list.currentIndex = startIndex !== undefined ? startIndex : Math.max(0, options.findIndex(o => o.checked))
        visible = true
        list.forceActiveFocus()
    }

    function close() {
        if (!visible) return
        visible = false
        if (returnFocus) returnFocus.forceActiveFocus()
        closed()
    }

    anchors.fill: parent
    visible: false
    z: 100

    Rectangle {
        anchors.fill: parent
        color: Theme.scrim
        opacity: root.dock === "center" ? 1 : 0.5
    }

    Rectangle {
        id: panel
        color: Theme.blade
        border.color: Theme.bladeEdge
        border.width: root.dock === "center" ? 1 : 0
        radius: root.dock === "center" ? Theme.px(12) : 0
        width: root.dock === "center" ? Theme.px(760) : Theme.px(510)
        height: root.dock === "center" ? Math.min(root.height * 0.8, header.height + list.contentHeight + Theme.px(60)) : root.height
        x: root.dock === "center" ? (root.width - width) / 2 : 0
        y: root.dock === "center" ? (root.height - height) / 2 : 0

        Text {
            id: header
            x: Theme.px(30)
            y: Theme.px(26)
            width: parent.width - Theme.px(60)
            height: implicitHeight + Theme.px(16)
            text: root.title
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.px(30)
            font.bold: true
            font.capitalization: Theme.caps
            color: Theme.highlight
        }

        ListView {
            id: list
            anchors.top: header.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.px(24)
            clip: true
            keyNavigationWraps: true
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: Theme.animation

            delegate: Item {
                id: option
                required property int index
                required property var modelData
                readonly property bool current: ListView.isCurrentItem
                width: list.width
                height: Theme.px(64)

                Rectangle {
                    anchors.fill: parent
                    color: Theme.highlight
                    visible: option.current
                }
                Text {
                    id: check
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.px(26)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.px(34)
                    text: option.modelData.checked ? "✓" : ""
                    font.pixelSize: Theme.rowFont
                    color: option.current ? Theme.highlightText : Theme.highlight
                }
                Text {
                    anchors.left: check.right
                    anchors.right: detail.left
                    anchors.rightMargin: Theme.px(12)
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: option.modelData.title
                    font.family: Theme.fontFamily
                    font.capitalization: Theme.caps
                    font.pixelSize: Theme.rowFont
                    font.bold: option.current
                    color: option.current ? Theme.highlightText : (option.modelData.enabled === false ? Theme.faint : Theme.text)
                }
                Text {
                    id: detail
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.px(26)
                    anchors.verticalCenter: parent.verticalCenter
                    text: option.modelData.detail || ""
                    font.family: Theme.fontFamily
                    font.capitalization: Theme.caps
                    font.pixelSize: Theme.smallFont
                    color: option.current ? Theme.highlightText : Theme.dim
                }
            }

            Keys.onReturnPressed: (event) => {
                const option = root.options[currentIndex]
                if (option && option.enabled === false) return
                const callback = root.callback
                const index = currentIndex
                root.close()
                if (callback) callback(index)
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Back || event.key === Qt.Key_Menu
                        || (root.dock === "left" && event.key === Qt.Key_Right)) {
                    root.close()
                    event.accepted = true
                } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_HomePage) {
                    event.accepted = true
                }
            }
        }
    }
}
