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

    property bool _frontNext: true

    onSourceChanged: {
        if (source === "") {
            front.opacity = 0
            back.opacity = 0
            return
        }
        if ((front.opacity > 0 && front.source == source) || (back.opacity > 0 && back.source == source)) return
        // Load into whichever layer is hidden, then fade it in.
        if (front.opacity >= back.opacity) back.source = source
        else front.source = source
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
        opacity: root.dim
    }
}
