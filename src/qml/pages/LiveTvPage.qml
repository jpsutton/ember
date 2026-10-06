// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember
import Ember.Player

// Live TV: the channel guide, laid out and driven like couchbox-iptv's.
// Details of the focused programme at the top left and the preview at the
// top right; under them the filter chips, then the grid: channels down the
// left, three hours of programmes across in half-hour columns, and a red
// line at now.
//
// The focus is one time shared by all rows: Up and Down change the channel
// and keep the time, Left and Right step it programme by programme (half
// hours where there is no guide), never into the past. Channel +/- page the
// rows, digits jump to a channel number, Menu offers favourites, Back
// returns the focus to now and then leaves.
//
// One player serves the preview and full screen: OK on a channel tunes it
// and fills the screen; Back returns to the guide with it still playing in
// the preview, which keeps the last channel watched (it doesn't follow the
// focus). Full screen, Up/Down and Channel +/- browse channels in the
// banner and OK tunes the one shown; Left/Right skip within what the cache
// holds, Play/Pause pauses (the stream keeps buffering).
FocusScope {
    id: page
    objectName: "livetv"

    property var app

    LiveGuide { id: guide }

    // Geometry, on Amber's 1920x1080 grid (couchbox-iptv's numbers).
    readonly property real margin: Theme.px(48)
    readonly property real pageTop: Theme.px(32)
    readonly property real previewWidth: Theme.px(480)
    readonly property real previewHeight: Theme.px(270)
    readonly property real channelColumn: Theme.px(340)
    readonly property real rowHeight: Theme.px(84)
    readonly property real rulerHeight: Theme.px(40)
    readonly property real gridTop: pageTop + previewHeight + Theme.px(20) + Theme.px(52) + Theme.px(12)
    readonly property real gridWidth: width - 2 * margin
    readonly property real pxPerSecond: (gridWidth - channelColumn) / guide.span()

    readonly property var channels: guide.channels
    property int row: 0
    property real focusTime: guide.now
    property real windowStart: guide.floorToSlot(guide.now)
    readonly property real windowEnd: windowStart + guide.span()
    property bool inChips: false
    // The channel the preview plays (the last one watched).
    property string playingId
    property bool fullScreen: false
    // Full screen: a channel browsed to in the banner, not yet tuned.
    property int browseRow: -1
    // Skipping within the cache (as PlayerPage, Plezy's way).
    property real skipTotal: 0
    property bool skipForward: true
    property real skipTarget: -1

    readonly property string settingsKey: "livetv-" + Session.userId
    readonly property int playingRow: guide.revision >= 0 ? guide.rowForChannel(playingId) : -1
    // The channel the banner is about: the one browsed to, or the one on.
    readonly property int bannerRow: browseRow >= 0 ? browseRow : playingRow
    readonly property var bannerChannel: channels[bannerRow] || ({})
    readonly property var nowOn: guide.revision >= 0 && bannerRow >= 0 ? guide.programmeAt(bannerRow, guide.now) : ({})
    readonly property var upNext: guide.revision >= 0 && bannerRow >= 0 ? guide.programmeAfter(bannerRow, guide.now) : ({})

    readonly property var channel: channels[row] || ({})
    // The focused programme; the revision is read so a guide reload updates it.
    readonly property var programme: guide.revision >= 0 && channels.length > 0 ? guide.programmeAt(row, focusTime) : ({})

    function timeX(t) {
        const clamped = Math.max(windowStart, Math.min(windowEnd, t))
        return channelColumn + (clamped - windowStart) * pxPerSecond
    }

    function setRow(next) {
        row = Math.max(0, Math.min(channels.length - 1, next))
        rows.positionViewAtIndex(row, ListView.Contain)
    }

    function stepFocus(direction) {
        focusTime = guide.step(row, focusTime, direction)
        windowStart = guide.windowFor(focusTime, windowStart)
    }

    function backToNow() {
        focusTime = guide.now
        windowStart = guide.floorToSlot(guide.now)
    }

    function pageRows() { return Math.max(1, Math.floor(rows.height / rowHeight)) }

    function options() {
        if (channels.length === 0) return
        const ch = channel
        page.app.menu.open(ch.number + "  " + ch.name, [
            { title: qsTr("Watch") },
            { title: ch.isFavorite ? qsTr("Remove from favourites") : qsTr("Add to favourites") },
        ], (i) => {
            if (i === 0) page.watch(row)
            else guide.setFavorite(ch.id, !ch.isFavorite)
        })
    }

    function tune(id) {
        playingId = id
        playback.play(id, true)
        ViewSettings.save(settingsKey, { channel: id })
    }

    // OK in the guide: the focused channel, full screen. The one already in
    // the preview carries on as it is.
    function watch(index) {
        const ch = channels[index]
        if (!ch) return
        const on = ch.id === playingId && (playback.state === Playback.Playing || playback.state === Playback.Loading)
        if (!on) tune(ch.id)
        fullScreen = true
        browseRow = -1
        showBanner()
    }

    function leaveFullScreen() {
        fullScreen = false
        browseRow = -1
        if (playingRow >= 0) setRow(playingRow)
    }

    // Full screen: a neighbouring channel in the banner, without tuning.
    function browse(step) {
        if (channels.length === 0) return
        const from = browseRow >= 0 ? browseRow : Math.max(0, playingRow)
        browseRow = (from + step + channels.length) % channels.length
        showBanner()
    }

    property bool bannerShown: false
    function showBanner() {
        bannerShown = true
        bannerTimer.restart()
    }

    function skip(seconds) {
        if (!playback.live || playback.state !== Playback.Playing) return
        const forward = seconds > 0
        // A press the other way, or after the readout went, starts a new run.
        const continuing = skipBadgeTimer.running && skipForward === forward
        const base = continuing && skipTarget >= 0 ? skipTarget : playback.position
        const target = Math.max(playback.seekableStart, Math.min(playback.liveEdge - 3, base + seconds))
        if (!continuing && Math.round(Math.abs(target - base)) === 0) return
        skipTotal = (continuing ? skipTotal : 0) + Math.abs(target - base)
        skipForward = forward
        skipTarget = target
        playback.seekTo(target)
        skipBadgeTimer.restart()
    }

    function playerMenu() {
        const options = []
        const actions = []
        const audio = playback.audioTracks
        const subs = playback.subtitleTracks
        const currentAudio = audio.find(t => t.selected)
        const currentSub = subs.find(t => t.selected)
        const ch = channels[playingRow] || {}
        options.push({ title: qsTr("Audio"), detail: currentAudio ? currentAudio.title : "" })
        actions.push(() => page.app.menu.open(qsTr("Audio"), audio.map(t => ({ title: t.title, checked: t.selected })),
                                              (i) => playback.selectAudio(audio[i].index)))
        options.push({ title: qsTr("Subtitles"), detail: currentSub ? currentSub.title : qsTr("Off") })
        actions.push(() => {
            const list = [{ title: qsTr("Off"), checked: !currentSub }].concat(subs.map(t => ({ title: t.title, checked: t.selected })))
            page.app.menu.open(qsTr("Subtitles"), list, (i) => playback.selectSubtitle(i === 0 ? -1 : subs[i - 1].index))
        })
        if (ch.id) {
            options.push({ title: ch.isFavorite ? qsTr("Remove from favourites") : qsTr("Add to favourites") })
            actions.push(() => guide.setFavorite(ch.id, !ch.isFavorite))
        }
        if (playback.behindLive > 1) {
            options.push({ title: qsTr("Back to live") })
            actions.push(() => playback.backToLive())
        }
        options.push({ title: qsTr("Back to the guide") })
        actions.push(() => page.leaveFullScreen())
        page.app.menu.open(ch.name || qsTr("Live TV"), options, (i) => actions[i]())
    }

    // Digits typed on the remote: a channel number, applied after a pause.
    property string digits
    Timer {
        id: digitTimer
        interval: 1500
        onTriggered: {
            const typed = page.digits
            page.digits = ""
            let target = guide.rowForNumber(typed)
            if (target < 0 && guide.hasNumber(typed) && guide.filter !== "all") {
                guide.filter = "all"
                target = guide.rowForNumber(typed)
            }
            // "41" on a remote without a dot means 4.1.
            if (target < 0 && typed.length > 1 && typed.indexOf(".") < 0) {
                for (let cut = typed.length - 1; cut > 0 && target < 0; --cut) {
                    target = guide.rowForNumber(typed.slice(0, cut) + "." + typed.slice(cut))
                }
            }
            if (target < 0) return
            if (page.fullScreen) {
                page.browseRow = -1
                page.tune(page.channels[target].id)
                page.showBanner()
            } else {
                page.setRow(target)
            }
        }
    }

    // Past programmes can't be watched: the focus moves on with the clock.
    Connections {
        target: guide
        function onNowChanged() {
            if (page.focusTime < guide.now) page.focusTime = guide.now
            page.windowStart = guide.windowFor(page.focusTime, page.windowStart)
        }
        function onChannelsChanged() {
            if (page.row >= page.channels.length) page.row = Math.max(0, page.channels.length - 1)
            // The first time: open on the preview's channel.
            if (!page.placed && page.channels.length > 0) {
                page.placed = true
                const saved = guide.rowForChannel(ViewSettings.load(page.settingsKey).channel || "")
                if (saved >= 0) {
                    page.row = saved
                    rows.positionViewAtIndex(Math.max(0, saved - 2), ListView.Beginning)
                }
            }
        }
    }

    property bool placed: false

    Playback {
        id: playback
        video: video
    }

    // The video plane under the window, in the preview box or on the whole
    // screen; nothing opaque may cover it.
    MpvVideo {
        id: video
        x: page.fullScreen ? 0 : preview.x
        y: page.fullScreen ? 0 : preview.y
        width: page.fullScreen ? page.width : preview.width
        height: page.fullScreen ? page.height : preview.height
    }

    ScreenInhibitor {
        active: playback.state === Playback.Playing && !playback.paused
    }

    Mpris {
        status: playback.state === Playback.Playing ? (playback.paused ? "Paused" : "Playing") : "Stopped"
        title: page.nowOn.title || page.bannerChannel.name || ""
        subtitle: page.bannerChannel.name || ""
        artUrl: page.bannerChannel.logo || ""
        onPlayRequested: playback.setPaused(false)
        onPauseRequested: playback.setPaused(true)
        onPlayPauseRequested: playback.togglePause()
        onStopRequested: playback.stop()
    }

    // The preview starts a moment after the page shows: a video plane made
    // before the window is up shows a frozen frame (couchbox-iptv's finding).
    Timer {
        interval: 800
        running: true
        onTriggered: {
            const saved = ViewSettings.load(page.settingsKey).channel || ""
            if (saved !== "") page.tune(saved)
        }
    }
    Component.onDestruction: if (playback.state === Playback.Playing || playback.state === Playback.Loading) playback.stop()

    Timer {
        id: bannerTimer
        interval: 5000
        onTriggered: {
            page.bannerShown = false
            page.browseRow = -1
        }
    }
    Timer {
        id: skipBadgeTimer
        interval: 1200
        onTriggered: page.skipTarget = -1
    }

    // The page's background, around the preview so the video shows through.
    Item {
        anchors.fill: parent
        visible: !page.fullScreen
        Rectangle { x: 0; y: 0; width: parent.width; height: page.pageTop; color: Theme.background }
        Rectangle { x: 0; y: page.pageTop; width: preview.x; height: page.previewHeight; color: Theme.background }
        Rectangle {
            x: preview.x + preview.width; y: page.pageTop
            width: parent.width - x; height: page.previewHeight
            color: Theme.background
        }
        Rectangle {
            x: 0; y: page.pageTop + page.previewHeight
            width: parent.width; height: parent.height - y
            color: Theme.background
        }
    }

    // The preview box: black until there is a picture, with what is going on.
    Rectangle {
        id: preview
        x: parent.width - page.margin - page.previewWidth
        y: page.pageTop
        width: page.previewWidth
        height: page.previewHeight
        color: "black"
        visible: !page.fullScreen
        opacity: playback.videoShown ? 0 : 1
        Text {
            anchors.centerIn: parent
            width: parent.width - Theme.px(40)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: playback.state === Playback.Failed ? playback.errorString
                  : playback.state === Playback.Loading ? qsTr("Tuning…") : ""
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFont
            color: playback.state === Playback.Failed ? Theme.error : Theme.dim
        }
    }

    // Details of the focused channel and programme.
    Column {
        x: page.margin
        y: page.pageTop
        width: preview.x - page.margin - Theme.px(32)
        height: page.previewHeight
        spacing: Theme.px(10)
        visible: page.channels.length > 0 && !page.fullScreen

        Row {
            spacing: Theme.px(16)
            ChannelLogo {
                size: Theme.px(64)
                source: page.channel.logo || ""
                name: page.channel.name || ""
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: (page.channel.number || "") + "  " + (page.channel.name || "") + (page.channel.isFavorite ? "  ★" : "")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.rowFont
                color: Theme.dim
            }
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: page.programme.title || qsTr("No guide information")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.titleFont
            font.bold: true
            color: Theme.text
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            visible: page.programme.start !== undefined
            text: page.programme.start === undefined ? ""
                  : guide.timeText(page.programme.start) + " – " + guide.timeText(page.programme.stop)
                    + (page.programme.episodeTitle ? "   " + page.programme.episodeTitle : "")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFont
            color: Theme.highlight
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
            text: page.programme.overview || ""
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: Theme.dim
        }
    }

    // Filter chips.
    Row {
        visible: !page.fullScreen
        x: page.margin
        y: page.pageTop + page.previewHeight + Theme.px(20)
        height: Theme.px(52)
        spacing: Theme.px(12)
        Repeater {
            model: [{ key: "all", title: qsTr("All") }, { key: "favourites", title: qsTr("Favourites") }]
            Rectangle {
                required property var modelData
                readonly property bool active: guide.filter === modelData.key
                width: chipText.implicitWidth + Theme.px(40)
                height: Theme.px(52)
                radius: height / 2
                color: active && page.inChips ? Theme.highlight : active ? Theme.cellFocusRow : Theme.cell
                Text {
                    id: chipText
                    anchors.centerIn: parent
                    text: parent.modelData.title
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallFont
                    font.bold: parent.active && page.inChips
                    color: parent.active && page.inChips ? Theme.highlightText : Theme.text
                }
            }
        }
    }

    // The grid.
    Item {
        id: grid
        visible: !page.fullScreen
        x: page.margin
        y: page.gridTop
        width: page.gridWidth
        height: parent.height - y - Theme.px(16)

        // Ruler: the time now over the channels, then the half hours.
        Text {
            y: Theme.px(4)
            text: guide.timeText(guide.now)
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFont
            font.bold: true
            color: Theme.text
        }
        Repeater {
            model: 6
            Text {
                required property int index
                x: page.channelColumn + index * 1800 * page.pxPerSecond + Theme.px(8)
                y: Theme.px(4)
                text: guide.timeText(page.windowStart + index * 1800)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFont
                color: Theme.dim
            }
        }

        ListView {
            id: rows
            y: page.rulerHeight
            width: parent.width
            height: parent.height - y
            clip: true
            interactive: false
            model: page.channels
            cacheBuffer: 0

            delegate: Item {
                id: rowItem
                required property int index
                required property var modelData
                readonly property bool focusedRow: index === page.row && !page.inChips
                readonly property var cells: guide.revision >= 0
                                             ? guide.cells(index, page.windowStart, page.windowEnd) : []
                width: rows.width
                height: page.rowHeight

                // Channel.
                Rectangle {
                    y: Theme.px(3)
                    width: page.channelColumn - Theme.px(8)
                    height: parent.height - Theme.px(6)
                    radius: Theme.px(6)
                    color: rowItem.focusedRow ? Theme.cellFocusRow : Theme.cell
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        x: Theme.px(10)
                        spacing: Theme.px(12)
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.px(64)
                            text: rowItem.modelData.number
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.smallFont
                            color: Theme.dim
                        }
                        ChannelLogo {
                            anchors.verticalCenter: parent.verticalCenter
                            source: rowItem.modelData.logo
                            name: rowItem.modelData.name
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: page.channelColumn - Theme.px(8) - Theme.px(10) - Theme.px(64) - Theme.px(52)
                                   - 3 * Theme.px(12) - Theme.px(34)
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            text: rowItem.modelData.name
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.smallFont
                            color: Theme.text
                        }
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.px(10)
                        anchors.verticalCenter: parent.verticalCenter
                        visible: rowItem.modelData.id === page.playingId && playback.state !== Playback.Idle
                        text: "▶"
                        font.pixelSize: Theme.smallFont
                        color: Theme.dim
                    }
                }

                // Programmes in the window.
                Repeater {
                    model: rowItem.cells
                    Rectangle {
                        required property var modelData
                        readonly property bool focused: rowItem.focusedRow
                                                        && modelData.start <= page.focusTime && modelData.stop > page.focusTime
                        x: page.timeX(modelData.start) + Theme.px(2)
                        y: Theme.px(3)
                        width: Math.max(0, page.timeX(modelData.stop) - page.timeX(modelData.start) - Theme.px(4))
                        height: rowItem.height - Theme.px(6)
                        radius: Theme.px(6)
                        color: focused ? Theme.highlight : Theme.cell
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.px(12)
                            anchors.rightMargin: Theme.px(8)
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            text: parent.modelData.title
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.smallFont
                            font.bold: parent.focused
                            color: parent.focused ? Theme.highlightText : Theme.text
                        }
                    }
                }

                // Nothing in the window: one dim cell across it.
                Rectangle {
                    visible: rowItem.cells.length === 0
                    x: page.channelColumn + Theme.px(2)
                    y: Theme.px(3)
                    width: rows.width - page.channelColumn - Theme.px(4)
                    height: rowItem.height - Theme.px(6)
                    radius: Theme.px(6)
                    color: Theme.cellEmpty
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        x: Theme.px(12)
                        text: qsTr("No information")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallFont
                        color: Theme.faint
                    }
                }

                // The focus in a gap: an empty half hour marks it.
                Rectangle {
                    visible: rowItem.focusedRow && page.programme.start === undefined
                    readonly property real slot: guide.floorToSlot(page.focusTime)
                    x: page.timeX(slot) + Theme.px(2)
                    y: Theme.px(3)
                    width: Math.max(0, page.timeX(slot + 1800) - page.timeX(slot) - Theme.px(4))
                    height: rowItem.height - Theme.px(6)
                    radius: Theme.px(6)
                    color: Theme.highlight
                }
            }
        }

        // Now.
        Rectangle {
            readonly property real nowX: page.channelColumn + (guide.now - page.windowStart) * page.pxPerSecond
            visible: nowX >= page.channelColumn && nowX <= grid.width
            x: nowX - Theme.px(1)
            y: Theme.px(32)
            width: Theme.px(3)
            height: grid.height - y
            color: Theme.live
        }

        Text {
            anchors.centerIn: rows
            visible: page.channels.length === 0
            width: rows.width - Theme.px(200)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: guide.errorString !== "" ? guide.errorString
                  : guide.loading ? qsTr("Loading the channels…")
                  : guide.filter === "favourites" ? qsTr("No favourites yet: press Menu on a channel to add it.")
                  : qsTr("No channels.")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: guide.errorString !== "" ? Theme.error : Theme.dim
        }
    }

    // Full screen: black until the picture comes, with what is going on.
    Rectangle {
        anchors.fill: parent
        visible: page.fullScreen && !playback.videoShown
        color: "black"
        Text {
            anchors.centerIn: parent
            width: parent.width * 0.6
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: playback.state === Playback.Failed ? playback.errorString + "\n" + qsTr("Up/Down for another channel")
                  : qsTr("Tuning…")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.rowFont
            color: playback.state === Playback.Failed ? Theme.error : Theme.dim
        }
    }

    SkipReadout {
        anchors.verticalCenter: parent.verticalCenter
        visible: page.fullScreen && opacity > 0
        total: page.skipTotal
        forward: page.skipForward
        shown: skipBadgeTimer.running
    }

    // Paused.
    Text {
        visible: page.fullScreen && playback.paused
        x: Theme.px(56)
        y: Theme.px(40)
        text: "❚❚  " + qsTr("Paused")
        font.family: Theme.fontFamily
        font.pixelSize: Theme.rowFont
        font.bold: true
        color: "white"
        style: Text.Outline
        styleColor: "#a0000000"
    }

    // The banner: the channel, what's on with its progress, and what's next.
    Item {
        id: banner
        visible: page.fullScreen && (page.bannerShown || !playback.videoShown) && page.bannerRow >= 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.px(330)

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.35; color: "#c0000000" }
                GradientStop { position: 1.0; color: "#f0000000" }
            }
        }

        Row {
            x: Theme.px(80)
            y: Theme.px(90)
            spacing: Theme.px(28)

            ChannelLogo {
                size: Theme.px(110)
                source: page.bannerChannel.logo || ""
                name: page.bannerChannel.name || ""
            }

            Column {
                width: banner.width - Theme.px(160) - Theme.px(110) - Theme.px(28)
                spacing: Theme.px(8)
                Item {
                    width: parent.width
                    height: channelLine.height
                    Text {
                        id: channelLine
                        text: (page.bannerChannel.number || "") + "  " + (page.bannerChannel.name || "")
                              + (page.bannerChannel.isFavorite ? "  ★" : "")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.px(44)
                        font.bold: true
                        color: Theme.text
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.baseline: channelLine.baseline
                        text: guide.timeText(guide.now)
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.bodyFont
                        color: Theme.dim
                    }
                }
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: page.nowOn.start === undefined ? qsTr("No guide information")
                          : guide.timeText(page.nowOn.start) + "–" + guide.timeText(page.nowOn.stop) + "  "
                            + page.nowOn.title + (page.nowOn.episodeTitle ? ": " + page.nowOn.episodeTitle : "")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.rowFont
                    color: page.nowOn.start === undefined ? Theme.dim : Theme.text
                }
                // Progress through the programme, by the clock.
                Rectangle {
                    visible: page.nowOn.start !== undefined
                    width: parent.width
                    height: Theme.px(8)
                    radius: height / 2
                    color: "#40ffffff"
                    Rectangle {
                        width: page.nowOn.start === undefined ? 0
                               : parent.width * Math.max(0, Math.min(1, (guide.now - page.nowOn.start) / (page.nowOn.stop - page.nowOn.start)))
                        height: parent.height
                        radius: height / 2
                        color: Theme.highlight
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    visible: text !== ""
                    text: page.nowOn.overview || ""
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallFont
                    color: Theme.dim
                }
                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    visible: page.upNext.start !== undefined
                    text: page.upNext.start === undefined ? ""
                          : qsTr("Next: %1  %2").arg(guide.timeText(page.upNext.start)).arg(page.upNext.title)
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallFont
                    color: Theme.dim
                }
                Text {
                    text: page.browseRow >= 0 ? qsTr("OK to watch")
                          : playback.behindLive > 1 ? qsTr("%1 behind live").arg(Format.clock(playback.behindLive))
                          : qsTr("Live")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallFont
                    font.bold: true
                    color: page.browseRow >= 0 ? Theme.highlight : playback.behindLive > 1 ? Theme.dim : Theme.live
                }
            }
        }
    }

    // Keys while the channel fills the screen.
    function playerKey(event) {
        switch (event.key) {
        case Qt.Key_Up:
        case Qt.Key_ChannelUp:
            browse(1)
            break
        case Qt.Key_Down:
        case Qt.Key_ChannelDown:
            browse(-1)
            break
        case Qt.Key_Return:
            if (browseRow >= 0) {
                const target = channels[browseRow]
                browseRow = -1
                if (target) tune(target.id)
            }
            showBanner()
            break
        case Qt.Key_Back:
            if (browseRow >= 0) browseRow = -1
            else leaveFullScreen()
            break
        case Qt.Key_MediaStop:
            playback.stop()
            leaveFullScreen()
            break
        case Qt.Key_Left:
            skip(-EmberSettings.seekStepSeconds)
            break
        case Qt.Key_Right:
            skip(EmberSettings.seekStepSeconds)
            break
        case Qt.Key_AudioRewind:
            skip(-EmberSettings.seekStepSeconds)
            break
        case Qt.Key_AudioForward:
            skip(EmberSettings.seekStepSeconds * 3)
            break
        case Qt.Key_MediaTogglePlayPause:
        case Qt.Key_Space:
            playback.togglePause()
            break
        case Qt.Key_MediaPlay:
            playback.setPaused(false)
            break
        case Qt.Key_MediaPause:
            playback.setPaused(true)
            break
        case Qt.Key_Menu:
            playerMenu()
            break
        case Qt.Key_Info:
            if (bannerShown) {
                bannerShown = false
                bannerTimer.stop()
            } else {
                showBanner()
            }
            break
        case Qt.Key_Guide:
            leaveFullScreen()
            break
        default:
            return false
        }
        event.accepted = true
        return true
    }

    Keys.onPressed: (event) => {
        if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9 || event.key === Qt.Key_Period) {
            page.digits = (page.digits + event.text).slice(-5)
            digitTimer.restart()
            event.accepted = true
            return
        }
        if (page.fullScreen) {
            page.playerKey(event)
            return
        }
        if (page.inChips) {
            const order = ["all", "favourites"]
            const at = order.indexOf(guide.filter)
            switch (event.key) {
            case Qt.Key_Left: guide.filter = order[Math.max(0, at - 1)]; page.row = 0; break
            case Qt.Key_Right: guide.filter = order[Math.min(order.length - 1, at + 1)]; page.row = 0; break
            case Qt.Key_Down:
            case Qt.Key_Return: page.inChips = false; break
            case Qt.Key_Back: page.inChips = false; break
            default: return
            }
            event.accepted = true
            return
        }
        switch (event.key) {
        case Qt.Key_Up:
            if (page.row > 0) page.setRow(page.row - 1)
            else page.inChips = true
            break
        case Qt.Key_Down: page.setRow(page.row + 1); break
        case Qt.Key_Left: page.stepFocus(-1); break
        case Qt.Key_Right: page.stepFocus(1); break
        case Qt.Key_PageUp:
        case Qt.Key_ChannelUp: page.setRow(page.row - page.pageRows()); break
        case Qt.Key_PageDown:
        case Qt.Key_ChannelDown: page.setRow(page.row + page.pageRows()); break
        case Qt.Key_Return:
        case Qt.Key_MediaPlay:
        case Qt.Key_MediaTogglePlayPause:
            page.watch(page.row)
            break
        case Qt.Key_Menu: page.options(); break
        case Qt.Key_Back:
            // First back to now, then out.
            if (page.focusTime > guide.now + 60) page.backToNow()
            else return
            break
        default:
            return
        }
        event.accepted = true
    }
}
