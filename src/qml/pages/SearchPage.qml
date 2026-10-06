// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// Type the search; results update as you go. Right from the field's last
// button, or OK, moves to the results.
FocusScope {
    id: page

    property var app

    ItemListModel {
        id: results
        mode: "search"
        includeTypes: "Movie,Series,Episode,BoxSet"
    }

    Timer {
        id: debounce
        interval: 400
        onTriggered: results.searchTerm = entry.text.trim()
    }

    Rectangle { anchors.fill: parent; color: Theme.background }

    TextEntry {
        id: entry
        x: Theme.px(100)
        y: Theme.px(110)
        width: implicitWidth
        focus: true
        label: qsTr("Search")
        onTextChanged: debounce.restart()
        onAccepted: if (list.count > 0) list.forceActiveFocus()
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Right && list.count > 0) {
                list.forceActiveFocus()
                event.accepted = true
            }
        }
    }

    Rectangle {
        x: Theme.px(1080)
        width: parent.width - x
        height: parent.height
        color: Theme.panel

        ListView {
            id: list
            anchors.fill: parent
            anchors.topMargin: Theme.px(110)
            anchors.bottomMargin: Theme.px(60)
            clip: true
            model: results
            keyNavigationWraps: true
            highlightMoveDuration: 0
            delegate: ItemRow {
                required property int index
                required property var item
                width: list.width
                current: ListView.isCurrentItem && list.activeFocus
                title: item.type === "Episode" ? item.seriesName + " · " + item.name : item.name
                label2: item.type === "Episode" ? item.episodeLabel : (item.type === "Series" ? qsTr("Show")
                        : item.type === "BoxSet" ? qsTr("Collection") : (item.year ? String(item.year) : ""))
                status: item.status
                unplayedCount: item.unplayedCount
                progress: item.playedPercentage
            }
            Keys.onReturnPressed: if (currentIndex >= 0) page.app.openItem(results.get(currentIndex))
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Left) {
                    entry.forceActiveFocus()
                    event.accepted = true
                } else if (event.key === Qt.Key_Info && currentIndex >= 0) {
                    page.app.openInfo(results.get(currentIndex))
                    event.accepted = true
                } else if (event.key === Qt.Key_Menu && currentIndex >= 0) {
                    page.app.itemMenu(results.get(currentIndex), results, {})
                    event.accepted = true
                }
            }
        }

        Text {
            anchors.centerIn: parent
            visible: list.count === 0
            text: results.loading ? qsTr("Searching…") : (results.searchTerm === "" ? qsTr("Type to search.") : qsTr("No matches."))
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: Theme.dim
        }
    }
}
