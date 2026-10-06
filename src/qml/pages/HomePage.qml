// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// Amber's home screen with the vertical menu: a dark blade on the left with
// the libraries, fanart from the focused library behind it, and a submenu
// that slides in on Left.
FocusScope {
    id: page

    property var app

    readonly property var menuItems: {
        const items = Session.shownLibraries.map(l => ({ title: l.name, kind: "library", library: l }))
        items.push({ title: qsTr("Search"), kind: "search" })
        items.push({ title: qsTr("Settings"), kind: "settings" })
        return items
    }
    readonly property var focused: menuItems[menu.currentIndex] || ({})

    // Submenu entries for a library, by its type.
    function submenuFor(library) {
        const types = page.app.libraryTypes(library.collectionType)
        const context = { libraryId: library.id, collectionType: library.collectionType, viewKey: library.id }
        const all = { mode: "items", parentId: library.id, includeTypes: types, recursive: types !== "" }
        const entries = []
        function add(title, query, extra) {
            entries.push({ title: title, query: query, context: Object.assign({}, context, extra || {}) })
        }
        if (library.collectionType === "movies") {
            add(qsTr("All movies"), all)
            add(qsTr("Recently added"), Object.assign({}, all, { sortBy: "DateCreated", descending: true }), { viewKey: library.id + "-recent", fixedSort: true })
            add(qsTr("In progress"), { mode: "resume", parentId: library.id, includeTypes: "Movie" }, { viewKey: "", fixedSort: true })
            add(qsTr("Unwatched"), Object.assign({}, all, { hideWatched: true }), { viewKey: library.id + "-unwatched" })
            add(qsTr("Genres"), { mode: "genres", parentId: library.id, includeTypes: "Movie" }, { viewKey: "", fixedSort: true })
            add(qsTr("Years"), { mode: "years", parentId: library.id, includeTypes: "Movie" }, { viewKey: "", fixedSort: true })
            add(qsTr("Collections"), { mode: "items", includeTypes: "BoxSet", recursive: true }, { viewKey: "collections" })
        } else if (library.collectionType === "tvshows") {
            add(qsTr("All shows"), all)
            add(qsTr("Next up"), { mode: "nextup", parentId: library.id }, { viewKey: "", fixedSort: true })
            add(qsTr("Recently added episodes"), { mode: "items", parentId: library.id, includeTypes: "Episode", recursive: true,
                                                   sortBy: "DateCreated", descending: true }, { viewKey: "", fixedSort: true })
            add(qsTr("In progress"), { mode: "resume", parentId: library.id, includeTypes: "Episode" }, { viewKey: "", fixedSort: true })
            add(qsTr("Unwatched shows"), Object.assign({}, all, { hideWatched: true }), { viewKey: library.id + "-unwatched" })
            add(qsTr("Genres"), { mode: "genres", parentId: library.id, includeTypes: "Series" }, { viewKey: "", fixedSort: true })
            add(qsTr("Years"), { mode: "years", parentId: library.id, includeTypes: "Series" }, { viewKey: "", fixedSort: true })
        } else {
            add(qsTr("Browse"), all)
            add(qsTr("Recently added"), { mode: "items", parentId: library.id, recursive: true, includeTypes: "Movie,Episode,Video",
                                          sortBy: "DateCreated", descending: true }, { viewKey: "", fixedSort: true })
            add(qsTr("In progress"), { mode: "resume", parentId: library.id }, { viewKey: "", fixedSort: true })
        }
        return entries
    }

    function activate(entry) {
        if (entry.kind === "library") page.app.openLibrary(entry.library)
        else if (entry.kind === "search") page.app.stack.push(page.app.searchPageComponent)
        else if (entry.kind === "settings") page.app.stack.push(page.app.settingsPageComponent)
    }

    function openSubmenu() {
        if (page.focused.kind !== "library") return
        submenu.model = submenuFor(page.focused.library)
        submenu.currentIndex = 0
        submenuOpen = true
        submenu.forceActiveFocus()
    }

    function closeSubmenu() {
        submenuOpen = false
        menu.forceActiveFocus()
    }

    property bool submenuOpen: false

    // Random fanart from the focused library, changing every 20 s.
    ItemListModel {
        id: fanart
        // Collections have no fanart of their own; use any film's.
        readonly property bool collections: page.focused.kind === "library" && page.focused.library.collectionType === "boxsets"
        mode: "items"
        parentId: page.focused.kind === "library" && !collections ? page.focused.library.id : ""
        includeTypes: page.focused.kind === "library" && !collections
                      ? (page.app.libraryTypes(page.focused.library.collectionType) || "Movie,Series") : "Movie,Series"
        recursive: true
        sortBy: "Random"
        onLoaded: backdropTimer.pick()
    }
    Timer {
        id: backdropTimer
        interval: 20000
        repeat: true
        running: page.visible
        property int cursor: 0
        function pick() {
            for (let i = 0; i < fanart.count; ++i) {
                const item = fanart.get((cursor + i) % fanart.count)
                if (item.backdrop) {
                    backdrop.source = item.backdrop
                    cursor = (cursor + i + 1) % fanart.count
                    return
                }
            }
        }
        onTriggered: pick()
    }

    Backdrop {
        id: backdrop
        anchors.fill: parent
        dim: 0.25
    }

    // Clock, top right, as on Amber's home.
    Text {
        id: clock
        anchors.right: parent.right
        anchors.rightMargin: Theme.px(60)
        anchors.top: parent.top
        anchors.topMargin: Theme.px(36)
        font.family: Theme.fontFamily
        font.capitalization: Theme.caps
        font.pixelSize: Theme.px(44)
        color: Theme.text
        style: Text.Outline
        styleColor: "#80000000"
        function update() { text = Qt.formatTime(new Date(), Qt.locale().timeFormat(Locale.ShortFormat)) }
        Timer { interval: 10000; running: true; repeat: true; triggeredOnStart: true; onTriggered: clock.update() }
    }

    // The blade.
    Rectangle {
        id: blade
        width: Theme.px(440)
        height: parent.height
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#f20c0d10" }
            GradientStop { position: 0.85; color: "#d90c0d10" }
            GradientStop { position: 1.0; color: "#a00c0d10" }
        }

        MenuList {
            id: menu
            x: Theme.px(30)
            y: Theme.px(60)
            width: Theme.px(380)
            height: parent.height - Theme.px(120)
            focus: true
            staticMode: EmberSettings.staticMenu
            fontFamily: Theme.menuFontFamily
            model: page.menuItems
            dimmed: page.submenuOpen
            onActivated: (index) => page.activate(page.menuItems[index])
            Keys.onPressed: (event) => {
                // Left or Back opens the submenu, as in Amber; Home is the
                // root, so Back has nowhere else to go.
                if (event.key === Qt.Key_Left || event.key === Qt.Key_Back) {
                    page.openSubmenu()
                    event.accepted = true
                } else if (event.key === Qt.Key_Menu) {
                    page.openSubmenu()
                    event.accepted = true
                }
            }
        }
    }

    // Library statistics under the clock.
    Text {
        anchors.right: parent.right
        anchors.rightMargin: Theme.px(60)
        anchors.top: clock.bottom
        anchors.topMargin: Theme.px(6)
        visible: page.focused.kind === "library" && fanart.totalCount > 0 && !fanart.collections
        text: (page.focused.title || "").toUpperCase() + "  " + fanart.totalCount
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallFont
        font.letterSpacing: Theme.px(1)
        color: Theme.dim
        style: Text.Outline
        styleColor: "#80000000"
    }

    // The submenu slides in over the blade.
    Rectangle {
        id: submenuPanel
        width: Theme.px(400)
        height: parent.height
        x: page.submenuOpen ? 0 : -width
        color: "#f2101116"
        visible: x > -width
        Behavior on x { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }

        // Short submenus sit in the middle of the screen, as in Amber.
        MenuList {
            id: submenu
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: Math.min(parent.height - Theme.px(120), rowHeight * count)
            staticMode: true
            barHighlight: true
            rowHeight: Theme.px(64)
            fontSize: Theme.rowFont
            horizontalAlignment: Text.AlignLeft
            dimmed: false
            onActivated: (index) => {
                const entry = submenu.model[index]
                page.closeSubmenu()
                page.app.openList(entry.title, entry.query, entry.context)
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Right || event.key === Qt.Key_Back || event.key === Qt.Key_Left) {
                    page.closeSubmenu()
                    event.accepted = true
                }
            }
        }
    }
}
