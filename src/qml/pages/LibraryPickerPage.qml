// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// Which libraries appear on the home menu. Shown after the first sign-in,
// and from Settings.
FocusScope {
    id: page
    objectName: "libraryPicker"

    property var app
    property bool firstRun: false

    readonly property var rows: {
        const list = Session.libraries.map(l => ({ title: l.name, checked: l.shown, id: l.id }))
        list.push({ title: qsTr("Done"), checked: false, id: "" })
        return list
    }

    Component.onCompleted: Session.refreshLibraries()

    Rectangle { anchors.fill: parent; color: Theme.background }

    Column {
        x: Theme.px(160)
        y: Theme.px(120)
        spacing: Theme.px(28)

        Text {
            text: qsTr("Libraries")
            font.family: Theme.fontFamily
            font.capitalization: Theme.caps
            font.pixelSize: Theme.titleFont
            font.bold: true
            color: Theme.text
        }
        Text {
            text: qsTr("Choose what the home menu shows. OK switches a library on or off; Menu moves it.")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: Theme.dim
        }

        ListView {
            id: list
            width: Theme.px(900)
            height: Math.min(Theme.px(64) * page.rows.length, Theme.px(760))
            clip: true
            focus: true
            keyNavigationWraps: true
            highlightMoveDuration: Theme.animation
            model: page.rows
            delegate: Item {
                id: row
                required property int index
                required property var modelData
                readonly property bool current: ListView.isCurrentItem
                width: list.width
                height: Theme.px(64)
                Rectangle { anchors.fill: parent; color: Theme.highlight; visible: row.current }
                Text {
                    x: Theme.px(24)
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.px(40)
                    text: row.modelData.id === "" ? "" : (row.modelData.checked ? "☑" : "☐")
                    font.pixelSize: Theme.rowFont
                    color: row.current ? Theme.highlightText : Theme.highlight
                }
                Text {
                    x: Theme.px(80)
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.title
                    font.family: Theme.fontFamily
                    font.capitalization: Theme.caps
                    font.pixelSize: Theme.rowFont
                    font.bold: row.current || row.modelData.id === ""
                    color: row.current ? Theme.highlightText : Theme.text
                }
            }
            Keys.onPressed: (event) => {
                if (event.key !== Qt.Key_Menu) return
                event.accepted = true
                const row = page.rows[currentIndex]
                if (row.id === "") return
                const index = currentIndex
                page.app.menu.open(row.title, [{ title: qsTr("Move up") }, { title: qsTr("Move down") }], (choice) => {
                    const delta = choice === 0 ? -1 : 1
                    Session.moveLibrary(row.id, delta)
                    list.currentIndex = Math.max(0, Math.min(Session.libraries.length - 1, index + delta))
                })
            }
            Keys.onReturnPressed: {
                const row = page.rows[currentIndex]
                if (row.id === "") {
                    Session.confirmLibraries()
                    if (!page.firstRun) page.app.stack.pop()
                } else {
                    // The rows are rebuilt on the change; keep the highlight.
                    const index = currentIndex
                    Session.setLibraryShown(row.id, !row.checked)
                    currentIndex = index
                }
            }
        }
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Back && page.firstRun) event.accepted = true
    }
}
