// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// Kodi's audio offset: a bar low over the picture that shifts the sound
// against it while playing. Left and Right move it 25 ms (held, they
// repeat), Up and Down 100 ms; OK sets it to 0 when pressed twice. Back,
// Menu or a few seconds untouched close it. The change applies at once.
FocusScope {
    id: root

    property var playback
    property Item returnFocus: null
    // The first OK only arms the reset, so a stray press keeps the setting.
    property bool resetArmed: false

    function open() {
        returnFocus = root.Window.activeFocusItem
        resetArmed = false
        visible = true
        forceActiveFocus()
        idleTimer.restart()
    }

    function close() {
        if (!visible) return
        visible = false
        idleTimer.stop()
        if (returnFocus) returnFocus.forceActiveFocus()
    }

    function step(milliseconds) {
        playback.audioDelay = playback.audioDelay + milliseconds
        resetArmed = false
        idleTimer.restart()
    }

    function label(milliseconds) {
        return milliseconds === 0 ? qsTr("0 ms") : (milliseconds > 0 ? "+" : "−") + Math.abs(milliseconds) + qsTr(" ms")
    }

    anchors.left: parent.left
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    height: Theme.px(300)
    visible: false
    z: 90

    Timer {
        id: idleTimer
        interval: 6000
        onTriggered: root.close()
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.px(60)
        width: Theme.px(900)
        height: Theme.px(200)
        radius: Theme.px(12)
        color: Theme.blade
        border.color: Theme.bladeEdge
        border.width: 1

        Text {
            x: Theme.px(30)
            y: Theme.px(22)
            text: qsTr("Audio offset")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.px(30)
            font.bold: true
            color: Theme.highlight
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.px(30)
            y: Theme.px(22)
            text: root.playback ? root.label(root.playback.audioDelay) : ""
            font.family: Theme.fontFamily
            font.pixelSize: Theme.px(30)
            font.bold: true
            color: Theme.text
        }

        // -1 s to +1 s, with the centre marked; further out the knob waits
        // at the end.
        Item {
            id: track
            x: Theme.px(30)
            y: Theme.px(92)
            width: parent.width - Theme.px(60)
            height: Theme.px(10)
            Rectangle { anchors.fill: parent; radius: height / 2; color: "#40ffffff" }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.px(3)
                height: Theme.px(26)
                color: Theme.dim
            }
            Rectangle {
                readonly property real fraction: root.playback ? Math.max(-1, Math.min(1, root.playback.audioDelay / 1000)) : 0
                x: track.width / 2 + fraction * track.width / 2 - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.px(26)
                height: width
                radius: width / 2
                color: Theme.highlight
            }
        }

        Text {
            x: Theme.px(30)
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.px(20)
            text: root.resetArmed ? qsTr("OK again: back to 0")
                                  : qsTr("Left/Right: 25 ms  ·  Up/Down: 100 ms  ·  + plays the sound later")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFont
            color: Theme.dim
        }
    }

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Left: root.step(-25); break
        case Qt.Key_Right: root.step(25); break
        case Qt.Key_Down: root.step(-100); break
        case Qt.Key_Up: root.step(100); break
        case Qt.Key_Return:
            if (root.resetArmed) {
                root.playback.audioDelay = 0
                root.resetArmed = false
            } else {
                root.resetArmed = root.playback.audioDelay !== 0
            }
            idleTimer.restart()
            break
        case Qt.Key_Back:
        case Qt.Key_Menu:
            root.close()
            break
        default:
            // Play/pause and the like still reach the player.
            return
        }
        event.accepted = true
    }
}
