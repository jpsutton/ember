// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Window
import Ember.Player

Window {
    id: window

    required property string source
    required property bool windowed
    required property string renderLoop

    property bool pip: false
    property var stats: ({})

    width: 1280
    height: 720
    visibility: windowed ? Window.Windowed : Window.FullScreen
    visible: true
    color: "transparent"
    title: "Ember plane spike"

    function stat(name) {
        const value = stats[name]
        if (value === undefined || value === null || value === "") return "-"
        if (typeof value === "number") return Number.isInteger(value) ? value : value.toFixed(2)
        return value
    }

    function clock(seconds) {
        if (typeof seconds !== "number") return "-:--"
        const s = Math.floor(seconds)
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }

    // Dark frame around the video. The window is transparent, so anything
    // drawn over the video's rectangle would hide it.
    Rectangle { color: "#101010"; x: 0; y: 0; width: window.width; height: video.y }
    Rectangle { color: "#101010"; x: 0; y: video.y + video.height; width: window.width; height: window.height - y }
    Rectangle { color: "#101010"; x: 0; y: video.y; width: video.x; height: video.height }
    Rectangle { color: "#101010"; x: video.x + video.width; y: video.y; width: window.width - x; height: video.height }

    MpvVideo {
        id: video

        x: window.pip ? window.width * 0.55 : 0
        y: window.pip ? window.height * 0.08 : 0
        width: window.pip ? window.width * 0.4 : window.width
        height: window.pip ? window.height * 0.4 : window.height

        Behavior on x { NumberAnimation { duration: 400; easing.type: Easing.InOutQuad } }
        Behavior on y { NumberAnimation { duration: 400; easing.type: Easing.InOutQuad } }
        Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.InOutQuad } }
        Behavior on height { NumberAnimation { duration: 400; easing.type: Easing.InOutQuad } }

        Component.onCompleted: {
            for (const name of ["hwdec-current", "video-codec", "width", "height", "estimated-vf-fps",
                                "display-fps", "frame-drop-count", "decoder-frame-drop-count",
                                "vo-delayed-frame-count", "time-pos", "duration", "pause"]) {
                observe(name, "node")
            }
            setOption("loop-file", "inf")
            command(["loadfile", window.source])
        }

        onMpvPropertyChanged: (name, value) => {
            const next = Object.assign({}, window.stats)
            next[name] = value
            window.stats = next
        }
        onMpvEvent: (name, data) => {
            if (name === "end-file" && data.reason === 4) status.text = "Playback failed: " + data.message
        }
    }

    Rectangle {
        id: overlay

        x: 40
        y: 40
        width: lines.implicitWidth + 48
        height: lines.implicitHeight + 40
        radius: 12
        color: "#c0000000"

        Column {
            id: lines
            x: 24
            y: 20
            spacing: 6

            Text { color: "#ffb300"; font.pixelSize: 28; font.bold: true; text: "Ember plane spike" }
            Text {
                color: "white"; font.pixelSize: 20
                text: "decode " + window.stat("hwdec-current") + " · " + window.stat("video-codec")
            }
            Text {
                color: "white"; font.pixelSize: 20
                text: window.stat("width") + "x" + window.stat("height") + " at " + window.stat("estimated-vf-fps")
                      + " fps · display " + window.stat("display-fps") + " Hz"
            }
            Text {
                color: "white"; font.pixelSize: 20
                text: "dropped " + window.stat("frame-drop-count") + " (vo) / " + window.stat("decoder-frame-drop-count")
                      + " (decoder) · delayed " + window.stat("vo-delayed-frame-count")
            }
            Text {
                color: "white"; font.pixelSize: 20
                text: window.clock(window.stats["time-pos"]) + " / " + window.clock(window.stats["duration"])
                      + (window.stats["pause"] === true ? "  (paused)" : "") + " · render loop " + window.renderLoop
            }
            Text {
                id: status
                color: "#ff8080"; font.pixelSize: 20
                text: video.errorString
                visible: text !== ""
            }
            Text {
                color: "#a0a0a0"; font.pixelSize: 18
                text: "OK pause · ←/→ seek 10 s · ↑ picture-in-picture · ↓ hide this panel\n"
                      + "Menu hide video · H hide window 2 s · M minimize · Back twice quit"
            }
        }
    }

    // For reading the numbers over SSH when nobody is at the screen.
    Timer {
        interval: 5000
        running: true
        repeat: true
        onTriggered: console.info("stats", JSON.stringify(window.stats))
    }

    Timer {
        id: quitArmed
        interval: 2000
    }

    Text {
        anchors.centerIn: parent
        visible: quitArmed.running
        color: "white"
        style: Text.Outline
        font.pixelSize: 36
        text: "Press Back again to quit"
    }

    Timer {
        id: reshow
        interval: 2000
        onTriggered: window.visible = true
    }

    Item {
        anchors.fill: parent
        focus: true

        Keys.onPressed: (event) => {
            switch (event.key) {
            case Qt.Key_Return:
            case Qt.Key_Enter:
            case Qt.Key_Select:
            case Qt.Key_Space:
            case Qt.Key_MediaPlay:
            case Qt.Key_MediaPause:
            case Qt.Key_MediaTogglePlayPause:
                video.command(["cycle", "pause"])
                break
            case Qt.Key_Left:
                video.command(["seek", "-10"])
                break
            case Qt.Key_Right:
                video.command(["seek", "10"])
                break
            case Qt.Key_Up:
                window.pip = !window.pip
                break
            case Qt.Key_Down:
                overlay.visible = !overlay.visible
                break
            case Qt.Key_Menu:
                video.visible = !video.visible
                break
            case Qt.Key_H:
                window.visible = false
                reshow.start()
                break
            case Qt.Key_M:
                window.showMinimized()
                break
            case Qt.Key_Back:
            case Qt.Key_Escape:
            case Qt.Key_Q:
                // Twice, so pressing every button to log it doesn't end the test.
                if (quitArmed.running) Qt.quit()
                quitArmed.start()
                break
            default:
                return
            }
            event.accepted = true
        }
    }
}
