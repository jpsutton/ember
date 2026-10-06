// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// Full-screen fanart that crossfades when the source changes, darkened so
// text stays readable over it.
Item {
    id: root

    property string source
    property real dim: 0.6

    clip: true

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    Image {
        id: front
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        sourceSize.width: Theme.px(1920)
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 400 } }
        onStatusChanged: if (status === Image.Ready) { opacity = 1; back.opacity = 0 }
    }

    Image {
        id: back
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        sourceSize.width: Theme.px(1920)
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 400 } }
        onStatusChanged: if (status === Image.Ready) { opacity = 1; front.opacity = 0 }
    }

    onSourceChanged: {
        if (source === "") {
            front.opacity = 0
            back.opacity = 0
            return
        }
        const shown = front.opacity >= back.opacity ? front : back
        const hidden = shown === front ? back : front
        if (String(shown.source) === source) return
        // Load into the hidden layer, then fade it in. When it already holds
        // this image, no status change will come, so swap now.
        if (String(hidden.source) === source && hidden.status === Image.Ready) {
            hidden.opacity = 1
            shown.opacity = 0
        } else {
            hidden.source = source
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
        opacity: root.dim
    }
}
