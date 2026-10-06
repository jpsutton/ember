// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Controls
import Ember

// Amber's List view: the list on the right third, the highlighted item's
// details on the left, its fanart behind. OK opens folders and plays
// videos; Left opens the view options; Right jumps by letter.
FocusScope {
    id: page

    property var app
    property string title
    property var query: ({})
    property var context: ({})
    // The item last played from here, refreshed when the player closes.
    property string playedId

    // Re-read when any row changes. The revision has to be used in the
    // expression: the QML compiler drops a bare read, and the dependency
    // with it.
    readonly property var current: items.revision >= 0 && list.currentIndex >= 0 ? items.get(list.currentIndex) : ({})
    readonly property bool sortable: items.mode === "items" && !context.fixedSort
    readonly property string viewKey: context.viewKey || ""

    ItemListModel {
        id: items
        onLoaded: {
            // A show with a single season goes straight to its episodes.
            if (mode === "seasons" && count === 1) {
                seasonId = get(0).id
                mode = "episodes"
                return
            }
            if (page.restoreId !== "") {
                const index = indexOfId(page.restoreId)
                if (index >= 0) list.currentIndex = index
                page.restoreId = ""
            }
        }
        onLetterFound: (index) => list.currentIndex = index
    }
    property string restoreId

    // Amber's list styles: "list" (List), "tall" (Tall List: more, smaller
    // rows), "big" (Big List: two-line rows with a poster).
    property string viewType: "list"
    readonly property real rowHeight: viewType === "tall" ? Theme.px(58) : viewType === "big" ? Theme.px(110) : Theme.px(72)

    Component.onCompleted: {
        for (const key in query) items[key] = query[key]
        // The list style is remembered per library, with a default for the
        // rest (seasons, episodes, genres).
        const defaults = ViewSettings.load("default")
        if (defaults.viewType) viewType = defaults.viewType
        if (viewKey !== "") {
            const saved = ViewSettings.load(viewKey)
            if (saved.viewType) viewType = saved.viewType
            if (sortable) {
                if (saved.sortBy) items.sortBy = saved.sortBy
                if (saved.descending !== undefined) items.descending = saved.descending === "true"
                if (saved.hideWatched !== undefined) items.hideWatched = saved.hideWatched === "true"
            }
        }
    }

    function saveView() {
        // Stay on the same item when the order changes.
        restoreId = current.id || ""
        ViewSettings.save(viewKey !== "" ? viewKey : "default", sortable
            ? { sortBy: items.sortBy, descending: String(items.descending),
                hideWatched: String(items.hideWatched), viewType: viewType }
            : { viewType: viewType })
    }

    // When the page shows again: reload if the server's library changed
    // meanwhile, and refresh what the player changed.
    StackView.onActivated: {
        if (items.stale) {
            restoreId = current.id || ""
            items.reload()
            playedId = ""
            return
        }
        if (playedId === "") return
        if (items.mode === "episodes" || items.mode === "seasons" || items.mode === "resume" || items.mode === "nextup") {
            restoreId = current.id || ""
            items.reload()
        } else {
            items.refreshItem(playedId)
        }
        playedId = ""
    }

    function label2(item) {
        switch (items.sortBy) {
        case "DateCreated": return item.dateCreated || ""
        case "PremiereDate": return item.premiereDate || (item.year ? String(item.year) : "")
        case "CommunityRating": return item.communityRating ? "★ " + item.communityRating : ""
        case "Runtime": return item.runtimeText || ""
        }
        if (item.type === "Episode") return items.mode === "episodes" ? (item.runtimeText || "") : (item.episodeLabel || "")
        if (item.type === "Season") return item.childCount ? qsTr("%1 episodes").arg(item.childCount) : ""
        return item.year ? String(item.year) : ""
    }

    function rowTitle(item) {
        if (item.type === "Episode") {
            if (items.mode === "episodes") return (item.indexNumber !== undefined ? item.indexNumber + ". " : "") + item.name
            return (item.seriesName ? item.seriesName + " · " : "") + item.name
        }
        return item.name
    }

    function open() {
        if (list.currentIndex < 0) return
        const item = current
        if (item.playable) page.playedId = item.id
        if (item.type === "Series" || item.type === "Season") page.playedId = item.id
        page.app.openItem(item, Object.assign({}, page.context, { series: page.context.series }))
    }

    function viewOptions() {
        const options = []
        const actions = []
        function add(title, detail, action) { options.push({ title: title, detail: detail }); actions.push(action) }
        const sorts = [
            ["SortName", qsTr("Name")], ["DateCreated", qsTr("Date added")], ["PremiereDate", qsTr("Release date")],
            ["CommunityRating", qsTr("Rating")], ["Runtime", qsTr("Runtime")], ["Random", qsTr("Random")]
        ]
        const views = [["list", qsTr("List")], ["tall", qsTr("Tall list")], ["big", qsTr("Big list")]]
        add(qsTr("View"), (views.find(v => v[0] === page.viewType) || views[0])[1], () => {
            page.app.menu.open(qsTr("View"), views.map(v => ({ title: v[1], checked: v[0] === page.viewType })),
                               (i) => { page.viewType = views[i][0]; page.saveView() }, "left")
        })
        if (sortable) {
            const currentSort = sorts.find(s => s[0] === items.sortBy) || sorts[0]
            add(qsTr("Sort by"), currentSort[1], () => {
                page.app.menu.open(qsTr("Sort by"), sorts.map(s => ({ title: s[1], checked: s[0] === items.sortBy })),
                                   (i) => { items.sortBy = sorts[i][0]; page.saveView() }, "left")
            })
            add(qsTr("Order"), items.descending ? qsTr("Descending") : qsTr("Ascending"), () => {
                items.descending = !items.descending
                page.saveView()
            })
            add(qsTr("Hide watched"), items.hideWatched ? qsTr("On") : qsTr("Off"), () => {
                items.hideWatched = !items.hideWatched
                page.saveView()
            })
        }
        add(qsTr("Search"), "", () => page.app.stack.push(page.app.searchPageComponent))
        add(qsTr("Home"), "", () => page.app.goHome())
        page.app.menu.open(qsTr("View options"), options, (i) => actions[i](), "left", 0)
    }

    Backdrop {
        anchors.fill: parent
        source: page.current.backdrop || (page.context.series ? page.context.series.backdrop || "" : "")
        dim: 0.62
    }

    DetailsPane {
        x: Theme.px(70)
        y: Theme.px(56)
        width: Theme.px(1150)
        height: parent.height - Theme.px(110)
        item: page.current
        visible: list.count > 0
    }

    Rectangle {
        id: panel
        x: Theme.px(1290)
        width: parent.width - x
        height: parent.height
        color: Theme.panel

        Text {
            id: header
            x: Theme.px(24)
            y: Theme.px(40)
            width: parent.width - Theme.px(48)
            text: page.title.toUpperCase()
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.px(28)
            font.bold: true
            font.letterSpacing: Theme.px(1)
            color: Theme.highlight
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.px(24)
            anchors.baseline: header.baseline
            text: list.count > 0 ? (list.currentIndex + 1) + " / " + items.totalCount : ""
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFont
            color: Theme.dim
        }

        ListView {
            id: list
            anchors.top: header.bottom
            anchors.topMargin: Theme.px(26)
            anchors.left: parent.left
            anchors.right: parent.right
            // Whole rows only: about 12 at the default size.
            readonly property int rows: Math.floor(Theme.px(870) / page.rowHeight)
            height: page.rowHeight * rows
            clip: true
            focus: true
            model: items
            keyNavigationWraps: true
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            highlightRangeMode: EmberSettings.centeredListFocus ? ListView.StrictlyEnforceRange : ListView.ApplyRange
            preferredHighlightBegin: page.rowHeight * (EmberSettings.centeredListFocus ? Math.floor(rows / 2) : 2)
            preferredHighlightEnd: page.rowHeight * (EmberSettings.centeredListFocus ? Math.floor(rows / 2) + 1 : rows - 2)
            cacheBuffer: height

            delegate: Loader {
                id: row
                required property int index
                required property var item
                readonly property bool isCurrent: ListView.isCurrentItem && list.activeFocus
                width: list.width
                height: page.rowHeight
                sourceComponent: page.viewType === "big" ? bigRow : plainRow
            }

            Component {
                id: plainRow
                ItemRow {
                    current: parent.isCurrent
                    compact: page.viewType === "tall"
                    title: page.rowTitle(parent.item)
                    label2: page.label2(parent.item)
                    status: parent.item.status
                    unplayedCount: parent.item.unplayedCount
                    progress: parent.item.playedPercentage
                }
            }

            Component {
                id: bigRow
                BigRow {
                    current: parent.isCurrent
                    item: parent.item
                    title: page.rowTitle(parent.item)
                }
            }

            // OK on an empty list that failed to load tries again.
            Keys.onReturnPressed: {
                if (count === 0 && items.errorString !== "") items.reload()
                else page.open()
            }
            Keys.onPressed: (event) => {
                switch (event.key) {
                case Qt.Key_PageDown:
                case Qt.Key_ChannelDown:
                    currentIndex = Math.min(count - 1, currentIndex + 10)
                    break
                case Qt.Key_PageUp:
                case Qt.Key_ChannelUp:
                    currentIndex = Math.max(0, currentIndex - 10)
                    break
                case Qt.Key_Info:
                    if (currentIndex >= 0 && page.current.type !== "Genre" && page.current.type !== "Year") {
                        page.app.openInfo(page.current, page.context)
                    }
                    break
                case Qt.Key_Menu:
                    if (currentIndex >= 0) page.app.itemMenu(page.current, items, page.context)
                    break
                case Qt.Key_Left:
                    page.viewOptions()
                    break
                case Qt.Key_Right:
                    if (page.sortable && items.sortBy === "SortName" && !items.descending && count > 0) {
                        alpha.open(page.current.sortName || page.current.name || "")
                    }
                    break
                case Qt.Key_MediaTogglePlayPause:
                case Qt.Key_MediaPlay:
                    if (page.current.playable) { page.playedId = page.current.id; page.app.play(page.current, false) }
                    break
                default:
                    return
                }
                event.accepted = true
            }
        }

        Text {
            anchors.centerIn: list
            visible: list.count === 0
            width: list.width - Theme.px(60)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: items.loading ? qsTr("Loading…")
                  : (items.errorString !== "" ? items.errorString + "\n" + qsTr("OK tries again.") : qsTr("Nothing here."))
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: items.errorString !== "" ? Theme.error : Theme.dim
        }

        // A-Z strip on the right edge (Right from the list).
        FocusScope {
            id: alpha
            anchors.right: parent.right
            anchors.top: list.top
            width: Theme.px(70)
            height: list.height
            visible: activeFocus

            readonly property var letters: "#ABCDEFGHIJKLMNOPQRSTUVWXYZ".split("")
            property int index: 0

            function open(name) {
                const first = (name || "").charAt(0).toUpperCase()
                const at = letters.indexOf(first)
                index = at > 0 ? at : 0
                forceActiveFocus()
            }

            Rectangle { anchors.fill: parent; color: "#ee0b0c0f" }
            Column {
                anchors.centerIn: parent
                Repeater {
                    model: alpha.letters
                    Text {
                        required property int index
                        required property string modelData
                        width: alpha.width
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData
                        font.family: Theme.fontFamily
                        font.pixelSize: index === alpha.index ? Theme.px(34) : Theme.px(24)
                        font.bold: index === alpha.index
                        color: index === alpha.index ? Theme.highlight : Theme.dim
                    }
                }
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                    index = (index + (event.key === Qt.Key_Down ? 1 : letters.length - 1)) % letters.length
                    items.findLetter(letters[index])
                } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Back || event.key === Qt.Key_Return
                           || event.key === Qt.Key_Right) {
                    list.forceActiveFocus()
                }
                event.accepted = true
            }
        }
    }
}
