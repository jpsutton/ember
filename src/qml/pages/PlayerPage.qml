// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember
import Ember.Player

// Full-screen playback. The video shows through the transparent window;
// everything here is drawn over it.
//
// Keys: OK or Play/Pause pauses, Left/Right seek, Up/Down and Channel +/-
// jump chapters, Menu opens audio and subtitle choices, Info toggles the
// panel, Back or Stop ends playback.
FocusScope {
    id: page

    property var app
    property string itemId
    property bool fromStart: false
    property bool osdVisible: true
    // The Up Next card was dismissed for this item.
    property bool nextDismissed: false
    property real nextCountdown: 0
    // Seeking with Left/Right: where the preview is.
    property real seekPreview: -1

    readonly property bool inIntro: playback.introStart >= 0 && playback.position >= playback.introStart
                                    && playback.position < playback.introEnd - 1
    readonly property bool nearEnd: playback.duration > 0 && (playback.creditsStart > 0
                                    ? playback.position >= playback.creditsStart
                                    : playback.duration - playback.position <= 30)
    readonly property bool showNext: playback.nextItem.id !== undefined && nearEnd && !nextDismissed
                                     && playback.state === Playback.Playing

    Playback {
        id: playback
        video: video
        onFinished: (completed) => {
            if (completed && playback.nextItem.id !== undefined && EmberSettings.autoPlayNext && !page.nextDismissed) {
                page.startNext()
            } else {
                page.close()
            }
        }
        onItemChanged: page.nextDismissed = false
        onSeeked: mpris.notifySeeked()
        // Show the panel briefly when playback starts.
        onStateChanged: if (state === Playback.Playing) page.showOsd()
    }

    function startNext() {
        nextDismissed = false
        nextTimer.stop()
        playback.playNext()
        showOsd()
    }

    function close() {
        if (page.app.stack.currentItem === page) page.app.stack.pop()
    }

    function showOsd() {
        osdVisible = true
        osdTimer.restart()
    }

    function seekBy(seconds) {
        if (playback.state !== Playback.Playing) return
        const base = seekPreview >= 0 ? seekPreview : playback.position
        seekPreview = Math.max(0, Math.min(playback.duration - 1, base + seconds))
        seekCommit.restart()
        showOsd()
    }

    function jumpChapter(direction) {
        const chapters = playback.chapters
        if (chapters.length === 0) { seekBy(direction * 60); return }
        let target = -1
        if (direction > 0) {
            for (const c of chapters) if (c.start > playback.position + 1) { target = c.start; break }
        } else {
            for (let i = chapters.length - 1; i >= 0; --i) if (chapters[i].start < playback.position - 3) { target = chapters[i].start; break }
            if (target < 0) target = 0
        }
        if (target >= 0) playback.seekTo(target)
        showOsd()
    }

    function trackMenu() {
        const options = []
        const actions = []
        const audio = playback.audioTracks
        const subs = playback.subtitleTracks
        const currentAudio = audio.find(t => t.selected)
        const currentSub = subs.find(t => t.selected)
        options.push({ title: qsTr("Audio"), detail: currentAudio ? currentAudio.title : "" })
        actions.push(() => page.app.menu.open(qsTr("Audio"), audio.map(t => ({ title: t.title, checked: t.selected })),
                                              (i) => playback.selectAudio(audio[i].index)))
        options.push({ title: qsTr("Subtitles"), detail: currentSub ? currentSub.title : qsTr("Off") })
        actions.push(() => {
            const list = [{ title: qsTr("Off"), checked: !currentSub }].concat(subs.map(t => ({ title: t.title, checked: t.selected })))
            page.app.menu.open(qsTr("Subtitles"), list, (i) => playback.selectSubtitle(i === 0 ? -1 : subs[i - 1].index))
        })
        if (playback.chapters.length > 0) {
            options.push({ title: qsTr("Chapters"), detail: String(playback.chapters.length) })
            actions.push(() => page.app.menu.open(qsTr("Chapters"),
                                                  playback.chapters.map(c => ({ title: c.title, detail: Format.clock(c.start) })),
                                                  (i) => playback.seekTo(playback.chapters[i].start)))
        }
        options.push({ title: qsTr("Stop"), detail: "" })
        actions.push(() => playback.stop())
        page.app.menu.open(qsTr("Playback"), options, (i) => actions[i]())
    }

    Component.onCompleted: playback.play(itemId, fromStart)
    Component.onDestruction: if (playback.state === Playback.Playing || playback.state === Playback.Loading) playback.stop()

    ScreenInhibitor {
        active: playback.state === Playback.Playing && !playback.paused
    }

    Mpris {
        id: mpris
        status: playback.state === Playback.Playing ? (playback.paused ? "Paused" : "Playing") : "Stopped"
        title: playback.subtitle !== "" && playback.item.type === "Episode" ? playback.subtitle : playback.title
        subtitle: playback.item.type === "Episode" ? playback.title : ""
        artUrl: playback.item.poster || ""
        duration: playback.duration
        position: playback.position
        canGoNext: playback.nextItem.id !== undefined
        onPlayRequested: playback.setPaused(false)
        onPauseRequested: playback.setPaused(true)
        onPlayPauseRequested: playback.togglePause()
        onStopRequested: playback.stop()
        onNextRequested: page.startNext()
        onSeekRequested: (s) => playback.seekRelative(s)
        onSetPositionRequested: (s) => playback.seekTo(s)
    }

    // Black until the first frame, so nothing behind the window shows.
    Rectangle {
        anchors.fill: parent
        color: "black"
        visible: !playback.videoShown
    }

    MpvVideo {
        id: video
        anchors.fill: parent
    }

    Timer {
        id: osdTimer
        interval: 5000
        onTriggered: if (!playback.paused && page.seekPreview < 0) page.osdVisible = false
    }

    Timer {
        id: seekCommit
        interval: 700
        onTriggered: {
            if (page.seekPreview >= 0) playback.seekTo(page.seekPreview)
            page.seekPreview = -1
            osdTimer.restart()
        }
    }

    Timer {
        id: nextTimer
        interval: 250
        repeat: true
        running: page.showNext && EmberSettings.autoPlayNext
        onRunningChanged: if (running) page.nextCountdown = EmberSettings.nextUpSeconds
        onTriggered: {
            page.nextCountdown -= 0.25
            if (page.nextCountdown <= 0) page.startNext()
        }
    }

    // Skips the intro on its own when that's switched on.
    Connections {
        target: playback
        function onPositionChanged() {
            if (EmberSettings.autoSkipIntro && page.inIntro) playback.seekTo(playback.introEnd)
        }
    }

    Text {
        anchors.centerIn: parent
        visible: playback.state === Playback.Loading || playback.buffering
        text: playback.state === Playback.Loading ? qsTr("Loading…") : qsTr("Buffering…")
        font.family: Theme.fontFamily
        font.pixelSize: Theme.rowFont
        color: Theme.text
        style: Text.Outline
        styleColor: "black"
    }

    Column {
        anchors.centerIn: parent
        width: parent.width * 0.6
        spacing: Theme.px(20)
        visible: playback.state === Playback.Failed
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: playback.errorString
            font.family: Theme.fontFamily
            font.pixelSize: Theme.rowFont
            color: Theme.error
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Press Back to return.")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: Theme.dim
        }
    }

    // Skip intro.
    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: Theme.px(80)
        anchors.bottom: osd.top
        anchors.bottomMargin: Theme.px(30)
        visible: page.inIntro && !EmberSettings.autoSkipIntro
        width: skipText.implicitWidth + Theme.px(60)
        height: Theme.px(70)
        radius: Theme.px(8)
        color: Theme.highlight
        Text {
            id: skipText
            anchors.centerIn: parent
            text: qsTr("OK: Skip intro")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.rowFont
            font.bold: true
            color: Theme.highlightText
        }
    }

    // Up next.
    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: Theme.px(80)
        anchors.bottom: osd.visible ? osd.top : parent.bottom
        anchors.bottomMargin: Theme.px(40)
        visible: page.showNext
        width: Theme.px(720)
        height: Theme.px(190)
        radius: Theme.px(12)
        color: Theme.blade
        border.color: Theme.highlight
        border.width: Theme.px(2)
        Image {
            id: nextThumb
            x: Theme.px(16)
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.px(256)
            height: Theme.px(144)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            source: playback.nextItem.thumb || playback.nextItem.poster || ""
        }
        Column {
            anchors.left: nextThumb.right
            anchors.leftMargin: Theme.px(20)
            anchors.right: parent.right
            anchors.rightMargin: Theme.px(20)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.px(6)
            Text {
                text: EmberSettings.autoPlayNext ? qsTr("Up next in %1").arg(Math.ceil(page.nextCountdown)) : qsTr("Up next")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFont
                font.bold: true
                color: Theme.highlight
            }
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: (playback.nextItem.episodeLabel || "") + "  " + (playback.nextItem.name || "")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.rowFont
                color: Theme.text
            }
            Text {
                text: qsTr("OK: play now · Back: keep watching")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFont
                color: Theme.dim
            }
        }
    }

    // The on-screen display.
    Item {
        id: osd
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.px(300)
        visible: page.osdVisible && playback.state === Playback.Playing

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#00000000" }
                GradientStop { position: 0.45; color: "#b0000000" }
                GradientStop { position: 1.0; color: "#e6000000" }
            }
        }

        Text {
            id: osdTitle
            x: Theme.px(80)
            y: Theme.px(70)
            width: parent.width - Theme.px(560)
            elide: Text.ElideRight
            text: playback.title
            font.family: Theme.fontFamily
            font.pixelSize: Theme.px(44)
            font.bold: true
            color: Theme.text
        }
        Text {
            anchors.left: osdTitle.left
            anchors.top: osdTitle.bottom
            width: osdTitle.width
            elide: Text.ElideRight
            text: playback.subtitle
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: Theme.dim
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.px(80)
            anchors.bottom: osdTitle.bottom
            horizontalAlignment: Text.AlignRight
            text: (playback.paused ? "❚❚  " : "")
                  + qsTr("Ends at %1").arg(Format.timeOfDay(playback.duration - playback.position))
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: Theme.text
        }

        // Seek bar.
        Item {
            id: bar
            x: Theme.px(80)
            y: Theme.px(190)
            width: parent.width - Theme.px(160)
            height: Theme.px(10)
            readonly property real shown: page.seekPreview >= 0 ? page.seekPreview : playback.position
            Rectangle { anchors.fill: parent; radius: height / 2; color: "#40ffffff" }
            Rectangle {
                width: playback.duration > 0 ? parent.width * Math.min(1, bar.shown / playback.duration) : 0
                height: parent.height
                radius: height / 2
                color: Theme.highlight
            }
            Repeater {
                model: playback.chapters
                Rectangle {
                    required property var modelData
                    visible: playback.duration > 0 && modelData.start > 0
                    x: bar.width * modelData.start / Math.max(1, playback.duration)
                    width: Theme.px(3)
                    height: bar.height
                    color: "#c0000000"
                }
            }

            // Trickplay thumbnail while seeking.
            Rectangle {
                id: preview
                readonly property var tp: playback.trickplay
                readonly property bool available: tp.url !== undefined && page.seekPreview >= 0
                visible: available && thumb.status === Image.Ready
                width: Theme.px(320)
                height: tp.width ? width * tp.height / tp.width : Theme.px(180)
                x: Math.max(0, Math.min(bar.width - width, bar.width * bar.shown / Math.max(1, playback.duration) - width / 2))
                // Above the panel, clear of the title.
                y: -bar.y - height - Theme.px(20)
                color: "black"
                border.color: Theme.highlight
                border.width: Theme.px(2)
                clip: true
                readonly property int frame: available ? Math.floor(page.seekPreview / Math.max(0.001, tp.interval)) : 0
                readonly property int perTile: available ? tp.tileWidth * tp.tileHeight : 1
                readonly property int tile: Math.floor(frame / perTile)
                readonly property int inTile: frame % perTile
                Image {
                    id: thumb
                    source: preview.available ? preview.tp.url.replace("%1", preview.tile) : ""
                    asynchronous: true
                    cache: true
                    sourceClipRect: preview.available
                        ? Qt.rect((preview.inTile % preview.tp.tileWidth) * preview.tp.width,
                                  Math.floor(preview.inTile / preview.tp.tileWidth) * preview.tp.height,
                                  preview.tp.width, preview.tp.height)
                        : Qt.rect(0, 0, 0, 0)
                    width: preview.width
                    height: preview.height
                }
            }
        }

        // Loads the tile for the current position while the panel is up, so
        // the first seek has its thumbnail ready.
        Image {
            visible: false
            asynchronous: true
            // Only the download matters; decode it small.
            sourceSize: Qt.size(400, 225)
            readonly property var tp: playback.trickplay
            source: tp.url !== undefined && osd.visible
                    ? tp.url.replace("%1", Math.floor(Math.floor(playback.position / Math.max(0.001, tp.interval))
                                                      / Math.max(1, tp.tileWidth * tp.tileHeight)))
                    : ""
        }

        Text {
            anchors.left: bar.left
            anchors.top: bar.bottom
            anchors.topMargin: Theme.px(16)
            text: Format.clock(bar.shown)
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: page.seekPreview >= 0 ? Theme.highlight : Theme.text
        }
        Text {
            anchors.right: bar.right
            anchors.top: bar.bottom
            anchors.topMargin: Theme.px(16)
            text: "-" + Format.clock(Math.max(0, playback.duration - bar.shown)) + "  /  " + Format.clock(playback.duration)
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: Theme.text
        }
    }

    Keys.onPressed: (event) => {
        const step = EmberSettings.seekStepSeconds
        switch (event.key) {
        case Qt.Key_Back:
            if (page.showNext) { page.nextDismissed = true; break }
            if (playback.state === Playback.Failed || playback.state === Playback.Idle) { page.close(); break }
            playback.stop()
            break
        case Qt.Key_MediaStop:
            playback.stop()
            if (playback.state !== Playback.Playing) page.close()
            break
        case Qt.Key_Return:
            if (page.showNext) page.startNext()
            else if (page.inIntro && !EmberSettings.autoSkipIntro) playback.seekTo(playback.introEnd)
            else { playback.togglePause(); page.showOsd() }
            break
        case Qt.Key_MediaTogglePlayPause:
        case Qt.Key_Space:
            playback.togglePause()
            page.showOsd()
            break
        case Qt.Key_MediaPlay:
            playback.setPaused(false)
            page.showOsd()
            break
        case Qt.Key_MediaPause:
            playback.setPaused(true)
            page.showOsd()
            break
        case Qt.Key_Left:
        case Qt.Key_MediaPrevious:
        case Qt.Key_AudioRewind:
            page.seekBy(-step)
            break
        case Qt.Key_Right:
        case Qt.Key_AudioForward:
            page.seekBy(step)
            break
        case Qt.Key_MediaNext:
            if (playback.nextItem.id !== undefined) page.startNext()
            else page.jumpChapter(1)
            break
        case Qt.Key_Up:
        case Qt.Key_ChannelUp:
            page.jumpChapter(1)
            break
        case Qt.Key_Down:
        case Qt.Key_ChannelDown:
            page.jumpChapter(-1)
            break
        case Qt.Key_Menu:
            page.trackMenu()
            break
        case Qt.Key_Info:
            if (page.osdVisible) page.osdVisible = false
            else page.showOsd()
            break
        default:
            page.showOsd()
            return
        }
        event.accepted = true
    }
}
