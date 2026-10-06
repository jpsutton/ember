// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// A channel's logo, or the initials of its name on a tile when it has none
// (or it won't load), as couchbox-iptv draws them.
Item {
    id: root

    property string source
    property string name
    property int size: Theme.px(52)

    width: size
    height: size

    Image {
        id: image
        anchors.fill: parent
        source: root.source
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        sourceSize: Qt.size(root.size * 2, root.size * 2)
    }

    Rectangle {
        anchors.fill: parent
        visible: image.status !== Image.Ready
        radius: Theme.px(8)
        color: "#1fffffff"
        Text {
            anchors.centerIn: parent
            text: root.name.split(/\s+/).filter(w => w !== "").slice(0, 2).map(w => w.charAt(0)).join("").toUpperCase()
            font.family: Theme.fontFamily
            font.pixelSize: Math.round(root.size * 0.36)
            font.bold: true
            color: Theme.text
        }
    }
}
