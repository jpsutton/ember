// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Controls
import Ember

// Amber's info dialog as a page: poster, metadata, plot, a row of buttons,
// and the cast.
FocusScope {
    id: page

    property var app
    property string itemId
    property var context: ({})
    property bool returning: false

    readonly property var item: details.item

    ItemDetails {
        id: details
        itemId: page.itemId
    }

    StackView.onActivated: if (returning) { details.reload(); returning = false }

    readonly property var buttons: {
        const list = []
        if (item.playable) {
            list.push({ title: item.status === "inProgress" ? qsTr("Resume") : qsTr("Play"), action: "play" })
            if (item.status === "inProgress") list.push({ title: qsTr("From beginning"), action: "restart" })
        } else if (item.type === "Series" || item.type === "Season") {
            list.push({ title: qsTr("Episodes"), action: "open" })
        } else if (item.isFolder) {
            list.push({ title: qsTr("Open"), action: "open" })
        }
        list.push({ title: item.played ? qsTr("Mark unwatched") : qsTr("Mark watched"), action: "played" })
        return list
    }

    function run(action) {
        switch (action) {
        case "play": returning = true; page.app.play(item, false); break
        case "restart": returning = true; page.app.play(item, true); break
        case "open": page.app.openItem(item, page.context); break
        case "played": details.setPlayed(!item.played); break
        }
    }

    Backdrop {
        anchors.fill: parent
        source: page.item.backdrop || ""
        dim: 0.72
    }

    Image {
        id: poster
        x: Theme.px(90)
        y: Theme.px(90)
        width: Theme.px(560)
        height: Theme.px(840)
        fillMode: Image.PreserveAspectFit
        verticalAlignment: Image.AlignTop
        asynchronous: true
        source: page.item.poster || ""
        sourceSize.height: Theme.px(840)
    }

    Column {
        x: poster.x + poster.width + Theme.px(60)
        y: Theme.px(90)
        width: parent.width - x - Theme.px(90)
        spacing: Theme.px(18)

        Text {
            width: parent.width
            text: page.item.type === "Episode" ? (page.item.seriesName || "") : (page.item.name || "")
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.px(64)
            font.bold: true
            color: Theme.text
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: page.item.type === "Episode" ? (page.item.episodeLabel + "  " + page.item.name)
                                                : (page.item.originalTitle && page.item.originalTitle !== page.item.name ? page.item.originalTitle : "")
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.px(36)
            color: Theme.text
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: [page.item.year, page.item.runtimeText, page.item.communityRating ? "★ " + page.item.communityRating : "",
                   page.item.officialRating, page.item.genres].filter(s => s).join("  ·  ")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: Theme.highlight
        }
        Text {
            width: parent.width
            visible: text !== ""
            wrapMode: Text.WordWrap
            text: page.item.tagline || ""
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            font.italic: true
            color: Theme.dim
        }

        ListView {
            id: buttonRow
            width: parent.width
            height: Theme.px(76)
            orientation: ListView.Horizontal
            spacing: Theme.px(16)
            focus: true
            model: page.buttons
            keyNavigationWraps: false
            delegate: Rectangle {
                id: button
                required property int index
                required property var modelData
                readonly property bool current: ListView.isCurrentItem && buttonRow.activeFocus
                width: label.implicitWidth + Theme.px(60)
                height: Theme.px(68)
                radius: Theme.px(8)
                color: current ? Theme.highlight : "#33ffffff"
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: button.modelData.title
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.rowFont
                    font.bold: button.current
                    color: button.current ? Theme.highlightText : Theme.text
                }
            }
            Keys.onReturnPressed: page.run(page.buttons[currentIndex].action)
        }

        Text {
            width: parent.width
            maximumLineCount: 7
            elide: Text.ElideRight
            wrapMode: Text.WordWrap
            lineHeight: 1.15
            text: page.item.overview || ""
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: Theme.text
        }
        Text {
            width: parent.width
            visible: text !== ""
            wrapMode: Text.WordWrap
            text: {
                const cast = details.people.filter(p => p.type === "Actor").slice(0, 8)
                    .map(p => p.role ? p.name + " (" + p.role + ")" : p.name)
                return cast.length ? qsTr("Cast: %1").arg(cast.join(", ")) : ""
            }
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFont
            color: Theme.dim
        }
        Text {
            width: parent.width
            visible: text !== ""
            wrapMode: Text.WordWrap
            text: {
                const crew = details.people.filter(p => p.type === "Director" || p.type === "Writer").slice(0, 4)
                    .map(p => p.name + " (" + (p.type === "Director" ? qsTr("director") : qsTr("writer")) + ")")
                return crew.join(", ")
            }
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFont
            color: Theme.dim
        }
        Text {
            width: parent.width
            text: [page.item.videoFlags, page.item.audioFlags, page.item.studios].filter(s => s).join("   |   ")
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFont
            font.bold: true
            color: Theme.dim
        }
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Info) {
            page.app.stack.pop()
            event.accepted = true
        }
    }
}
