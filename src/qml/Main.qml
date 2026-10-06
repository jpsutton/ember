// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Ember

Window {
    id: window

    required property bool windowed

    width: 1280
    height: 720
    visibility: windowed ? Window.Windowed : Window.FullScreen
    visible: true
    // Transparent: the video plane sits below the window.
    color: "transparent"
    title: "Ember"

    Binding { target: Theme; property: "screenHeight"; value: window.height }
    Binding { target: Theme; property: "scale"; value: EmberSettings.uiScale }

    // What a library's lists contain, by collection type.
    function libraryTypes(collectionType) {
        switch (collectionType) {
        case "movies": return "Movie"
        case "tvshows": return "Series"
        case "boxsets": return "BoxSet"
        case "musicvideos": return "MusicVideo"
        default: return ""
        }
    }

    // The page for a whole library (OK on a home menu item).
    function openLibrary(library) {
        const types = libraryTypes(library.collectionType)
        openList(library.name, {
            mode: "items", parentId: library.id, includeTypes: types, recursive: types !== ""
        }, { libraryId: library.id, collectionType: library.collectionType, viewKey: library.id })
    }

    function openList(title, query, context) {
        stack.push(libraryPage, { title: title, query: query, context: context || {} })
    }

    // OK on a list row: open folders, play everything else.
    function openItem(item, context) {
        context = context || {}
        switch (item.type) {
        case "Series":
            stack.push(libraryPage, {
                title: item.name, query: { mode: "seasons", seriesId: item.id },
                context: Object.assign({}, context, { series: item, viewKey: "" })
            })
            return
        case "Season":
            stack.push(libraryPage, {
                title: (item.seriesName ? item.seriesName + " · " : "") + item.name,
                query: { mode: "episodes", seriesId: item.seriesId, seasonId: item.id },
                context: Object.assign({}, context, { viewKey: "" })
            })
            return
        case "Genre":
            openList(item.name, {
                mode: "items", parentId: context.libraryId || "", genreId: item.id,
                includeTypes: libraryTypes(context.collectionType) || "Movie,Series", recursive: true
            }, Object.assign({}, context, { viewKey: (context.libraryId || "") + "-genre" }))
            return
        case "Year":
            openList(item.name, {
                mode: "items", parentId: context.libraryId || "", year: item.year,
                includeTypes: libraryTypes(context.collectionType) || "Movie,Series", recursive: true
            }, Object.assign({}, context, { viewKey: (context.libraryId || "") + "-year" }))
            return
        }
        if (item.playable) {
            play(item, false)
        } else {
            openList(item.name, { mode: "items", parentId: item.id }, Object.assign({}, context, { viewKey: item.id }))
        }
    }

    function play(item, fromStart) {
        stack.push(playerPage, { itemId: item.id, fromStart: fromStart === true })
    }

    function openInfo(item, context) {
        stack.push(infoPage, { itemId: item.id, context: context || {} })
    }

    function goHome() {
        if (Session.state === Session.SignedIn && Session.librariesChosen && stack.depth > 1) stack.pop(null)
    }

    // The context menu for an item (Menu key, or OK held).
    function itemMenu(item, model, context) {
        const options = []
        const actions = []
        function add(title, action) { options.push({ title: title }); actions.push(action) }
        if (item.playable) {
            add(item.status === "inProgress" ? qsTr("Resume from %1").arg(item.resumeText) : qsTr("Play"),
                () => play(item, false))
            if (item.status === "inProgress") add(qsTr("Play from beginning"), () => play(item, true))
        } else if (item.type !== "Genre" && item.type !== "Year") {
            add(qsTr("Open"), () => openItem(item, context))
        }
        if (item.type !== "Genre" && item.type !== "Year") {
            add(qsTr("Information"), () => openInfo(item, context))
            if (item.played) {
                add(qsTr("Mark as unwatched"), () => model.setPlayed(item.id, false))
            } else {
                add(qsTr("Mark as watched"), () => model.setPlayed(item.id, true))
            }
        }
        if (item.type === "Episode" && item.seriesId) {
            add(qsTr("Go to season"), () => openItem({ type: "Season", id: item.seasonId, name: item.seasonName,
                                                        seriesId: item.seriesId, seriesName: item.seriesName }, context))
            add(qsTr("Go to show"), () => openItem({ type: "Series", id: item.seriesId, name: item.seriesName }, context))
        }
        if (options.length === 0) return
        menu.open(item.name, options, (index) => actions[index]())
    }

    // Puts the right first page up for the session's state.
    function resetFlow() {
        if (Session.state === Session.NoServer) stack.replace(null, serverPage)
        else if (Session.state === Session.SignedOut) stack.replace(null, signInPage)
        else if (!Session.librariesChosen) stack.replace(null, libraryPickerPage, { firstRun: true })
        else stack.replace(null, homePage)
    }

    Connections {
        target: Session
        function onStateChanged() { window.resetFlow() }
        function onLibrariesChanged() {
            if (Session.state === Session.SignedIn && Session.librariesChosen && stack.currentItem
                    && stack.currentItem.objectName === "libraryPicker" && stack.currentItem.firstRun) {
                window.resetFlow()
            }
        }
    }

    Item {
        id: root
        anchors.fill: parent
        focus: true

        StackView {
            id: stack
            anchors.fill: parent
            focus: true
            pushEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animation } }
            pushExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animation } }
            popEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animation } }
            popExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animation } }
            replaceEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animation } }
            replaceExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animation } }
            onCurrentItemChanged: if (currentItem) currentItem.forceActiveFocus()
        }

        PopupMenu {
            id: menu
        }

        // Keys no page took.
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Back) {
                if (stack.depth > 1) stack.pop()
                event.accepted = true
            } else if (event.key === Qt.Key_HomePage) {
                window.goHome()
                event.accepted = true
            }
        }
    }

    Component { id: serverPage; ServerPage { app: window } }
    Component { id: signInPage; SignInPage { app: window } }
    Component { id: libraryPickerPage; LibraryPickerPage { app: window } }
    Component { id: homePage; HomePage { app: window } }
    Component { id: libraryPage; LibraryPage { app: window } }
    Component { id: infoPage; InfoPage { app: window } }
    Component { id: playerPage; PlayerPage { app: window } }
    Component { id: settingsPage; SettingsPage { app: window } }
    Component { id: searchPage; SearchPage { app: window } }

    property alias stack: stack
    property alias menu: menu
    property alias settingsPageComponent: settingsPage
    property alias searchPageComponent: searchPage
    property alias libraryPickerComponent: libraryPickerPage

    Component.onCompleted: resetFlow()
}
