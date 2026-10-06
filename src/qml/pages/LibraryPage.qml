// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Controls
import Ember

// Amber's list views: the list on the right third, the highlighted item's
// details on the left, its fanart behind (or, in Simple List, three columns
// of titles across the screen). OK opens folders and plays videos; Left
// opens the view options; Right jumps by letter.
FocusScope {
    id: page

    property var app
    property string title
    property var query: ({})
    property var context: ({})
    // The item last played from here, refreshed when the player closes.
    property string playedId
    property string restoreId

    // Amber's list styles:
    //   list    List: 12 rows beside the details
    //   low     Low List: 6 rows at the bottom, more of the fanart showing
    //   tall    Tall List: more, smaller rows
    //   big     Big List: two-line rows with a poster
    //   simple  Simple List: three columns of titles, no details
    property string viewType: "list"
    readonly property bool simple: viewType === "simple"
    readonly property real rowHeight: viewType === "tall" ? Theme.px(58) : viewType === "big" ? Theme.px(110)
                                      : simple ? Theme.px(64) : Theme.px(72)
    // The view that has the rows right now.
    readonly property Item view: simple ? grid : list

    // Re-read when any row changes. The revision has to be used in the
    // expression: the QML compiler drops a bare read, and the dependency
    // with it.
    readonly property var current: items.revision >= 0 && view.currentIndex >= 0 ? items.get(view.currentIndex) : ({})
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
                if (index >= 0) page.view.currentIndex = index
                page.restoreId = ""
            }
        }
        onLetterFound: (index) => page.view.currentIndex = index
    }

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

    // Changes the list style, keeping the highlighted item.
    function setViewType(type) {
        const id = current.id || ""
        viewType = type
        saveView()
        // After the views have swapped models.
        Qt.callLater(() => {
            page.view.currentIndex = Math.max(0, items.indexOfId(id))
            page.view.forceActiveFocus()
        })
    }

    // Re-sorts or re-filters, keeping the highlighted item when it stays.
    function requery(change) {
        restoreId = current.id || ""
        change()
        saveView()
    }

    function saveView() {
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
        if (items.mode === "aired") return item.airedDate || ""
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
        if (view.currentIndex < 0) return
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
        const views = [["list", qsTr("List")], ["low", qsTr("Low list")], ["tall", qsTr("Tall list")],
                       ["big", qsTr("Big list")], ["simple", qsTr("Simple list")]]
        // A whole library also offers its newest items, as lists of their own.
        if (context.libraryRoot && context.collectionType !== "boxsets") {
            add(qsTr("Recently added"), "", () => page.app.openRecent(page.title + " · " + qsTr("Recently added"), page.context, false))
            add(qsTr("Recently aired"), "", () => page.app.openRecent(page.title + " · " + qsTr("Recently aired"), page.context, true))
        }
        add(qsTr("View"), (views.find(v => v[0] === page.viewType) || views[0])[1], () => {
            page.app.menu.open(qsTr("View"), views.map(v => ({ title: v[1], checked: v[0] === page.viewType })),
                               (i) => page.setViewType(views[i][0]), "left")
        })
        if (sortable) {
            const currentSort = sorts.find(s => s[0] === items.sortBy) || sorts[0]
            add(qsTr("Sort by"), currentSort[1], () => {
                page.app.menu.open(qsTr("Sort by"), sorts.map(s => ({ title: s[1], checked: s[0] === items.sortBy })),
                                   (i) => page.requery(() => items.sortBy = sorts[i][0]), "left")
            })
            add(qsTr("Order"), items.descending ? qsTr("Descending") : qsTr("Ascending"),
                () => page.requery(() => items.descending = !items.descending))
            add(qsTr("Hide watched"), items.hideWatched ? qsTr("On") : qsTr("Off"),
                () => page.requery(() => items.hideWatched = !items.hideWatched))
        }
        add(qsTr("Search"), "", () => page.app.stack.push(page.app.searchPageComponent))
        add(qsTr("Home"), "", () => page.app.goHome())
        page.app.menu.open(qsTr("View options"), options, (i) => actions[i](), "left", 0)
    }

    function canJumpByLetter() {
        return sortable && items.sortBy === "SortName" && !items.descending && items.count > 0
    }

    // Keys both views share. |step| is how far a page moves.
    function handleKey(event, step) {
        switch (event.key) {
        case Qt.Key_Return:
            // OK on an empty list that failed to load tries again.
            if (view.count === 0 && items.errorString !== "") items.reload()
            else open()
            break
        case Qt.Key_PageDown:
        case Qt.Key_ChannelDown:
            view.currentIndex = Math.min(view.count - 1, view.currentIndex + step)
            break
        case Qt.Key_PageUp:
        case Qt.Key_ChannelUp:
            view.currentIndex = Math.max(0, view.currentIndex - step)
            break
        case Qt.Key_Info:
            if (view.currentIndex >= 0 && current.type !== "Genre" && current.type !== "Year") {
                app.openInfo(current, context)
            }
            break
        case Qt.Key_Menu:
            if (view.currentIndex >= 0) app.itemMenu(current, items, context)
            break
        case Qt.Key_MediaTogglePlayPause:
        case Qt.Key_MediaPlay:
            if (current.playable) { playedId = current.id; app.play(current, false) }
            break
        default:
            return false
        }
        event.accepted = true
        return true
    }

    Backdrop {
        anchors.fill: parent
        source: page.current.backdrop || (page.context.series ? page.context.series.backdrop || "" : "")
        dim: page.simple ? 0.82 : page.viewType === "low" ? 0.45 : 0.62
    }

    DetailsPane {
        x: Theme.px(70)
        y: page.viewType === "low" ? parent.height - Theme.px(770) : Theme.px(56)
        width: Theme.px(1150)
        height: parent.height - y - Theme.px(54)
        item: page.current
        visible: !page.simple && page.view.count > 0
    }

    Rectangle {
        id: panel
        x: page.simple ? 0 : Theme.px(1290)
        width: parent.width - x
        // Low List keeps the panel to the bottom part of the screen.
        y: page.viewType === "low" ? parent.height - height : 0
        height: page.viewType === "low" ? header.y + header.height + Theme.px(26) + list.height + Theme.px(30) : parent.height
        color: Theme.panel

        Text {
            id: header
            x: Theme.px(24)
            y: page.viewType === "low" ? Theme.px(24) : Theme.px(40)
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
            text: page.view.count > 0 ? (page.view.currentIndex + 1) + " / " + items.totalCount : ""
            font.family: Theme.fontFamily
            font.capitalization: Theme.caps
            font.pixelSize: Theme.smallFont
            color: Theme.dim
        }

        ListView {
            id: list
            visible: !page.simple
            anchors.top: header.bottom
            anchors.topMargin: Theme.px(26)
            anchors.left: parent.left
            anchors.right: parent.right
            // Whole rows only: about 12 at the default size, 6 in Low List.
            readonly property int rows: page.viewType === "low" ? 6 : Math.floor(Theme.px(870) / page.rowHeight)
            height: page.rowHeight * rows
            clip: true
            focus: !page.simple
            model: page.simple ? null : items
            keyNavigationWraps: true
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            highlightRangeMode: EmberSettings.centeredListFocus ? ListView.StrictlyEnforceRange : ListView.ApplyRange
            preferredHighlightBegin: page.rowHeight * (EmberSettings.centeredListFocus ? Math.floor(rows / 2) : Math.min(2, rows - 1))
            preferredHighlightEnd: page.rowHeight * (EmberSettings.centeredListFocus ? Math.floor(rows / 2) + 1 : Math.max(rows - 2, 1))
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

            Keys.onPressed: (event) => {
                if (page.handleKey(event, 10)) return
                if (event.key === Qt.Key_Left) {
                    page.viewOptions()
                    event.accepted = true
                } else if (event.key === Qt.Key_Right) {
                    if (page.canJumpByLetter()) alpha.open(page.current.sortName || page.current.name || "")
                    event.accepted = true
                }
            }
        }

        // Simple List: three columns, filled top to bottom.
        GridView {
            id: grid
            visible: page.simple
            anchors.top: header.bottom
            anchors.topMargin: Theme.px(26)
            anchors.left: parent.left
            anchors.leftMargin: Theme.px(40)
            anchors.right: parent.right
            anchors.rightMargin: Theme.px(110)
            readonly property int rows: Math.floor(Theme.px(900) / page.rowHeight)
            height: page.rowHeight * rows
            flow: GridView.FlowTopToBottom
            cellWidth: width / 3
            cellHeight: page.rowHeight
            clip: true
            focus: page.simple
            model: page.simple ? items : null
            keyNavigationWraps: false
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            cacheBuffer: width

            delegate: Item {
                id: cell
                required property int index
                required property var item
                readonly property bool isCurrent: GridView.isCurrentItem && grid.activeFocus
                width: grid.cellWidth
                height: grid.cellHeight
                ItemRow {
                    anchors.fill: parent
                    anchors.rightMargin: Theme.px(16)
                    current: cell.isCurrent
                    title: page.rowTitle(cell.item)
                    status: cell.item.status
                    unplayedCount: cell.item.unplayedCount
                    progress: cell.item.playedPercentage
                }
            }

            Keys.onPressed: (event) => {
                if (page.handleKey(event, rows)) return
                if (event.key === Qt.Key_Left) {
                    // From the first column, Left opens the view options.
                    if (currentIndex < rows) page.viewOptions()
                    else moveCurrentIndexLeft()
                    event.accepted = true
                } else if (event.key === Qt.Key_Right) {
                    // From the last column, Right opens the A-Z strip.
                    if (currentIndex + rows >= count) {
                        if (page.canJumpByLetter()) alpha.open(page.current.sortName || page.current.name || "")
                    } else {
                        moveCurrentIndexRight()
                    }
                    event.accepted = true
                }
            }
        }

        Text {
            anchors.centerIn: page.view
            visible: page.view.count === 0
            width: list.width - Theme.px(60)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: items.loading ? qsTr("Loading…")
                  : (items.errorString !== "" ? items.errorString + "\n" + qsTr("OK tries again.") : qsTr("Nothing here."))
            font.family: Theme.fontFamily
            font.capitalization: Theme.caps
            font.pixelSize: Theme.bodyFont
            color: items.errorString !== "" ? Theme.error : Theme.dim
        }

        // A-Z strip on the right edge (Right from the list).
        FocusScope {
            id: alpha
            anchors.right: parent.right
            anchors.top: page.view.top
            width: Theme.px(70)
            height: page.view.height
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
                    page.view.forceActiveFocus()
                }
                event.accepted = true
            }
        }
    }
}
