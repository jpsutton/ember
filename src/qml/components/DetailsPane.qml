// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// The left two-thirds of a list view: logo or title, poster, and what is
// known about the highlighted item.
Item {
    id: pane

    property var item: ({})

    readonly property bool isEpisode: item.type === "Episode"
    readonly property bool isSeason: item.type === "Season"

    function metaLine() {
        const parts = []
        if (item.runtimeText) parts.push(item.runtimeText)
        if (item.communityRating) parts.push("★ " + item.communityRating)
        if (item.officialRating) parts.push(item.officialRating)
        if (isEpisode && item.premiereDate) parts.push(item.premiereDate)
        if (item.type === "Series" && item.recursiveCount) parts.push(qsTr("%1 episodes").arg(item.recursiveCount))
        if (isSeason && item.childCount) parts.push(qsTr("%1 episodes").arg(item.childCount))
        if (item.status === "inProgress" && item.resumeText) parts.push(qsTr("Resume at %1").arg(item.resumeText))
        return parts.join("  ·  ")
    }

    function heading() {
        if (isEpisode) return item.seriesName || item.name
        if (isSeason) return item.seriesName || item.name
        return item.name || ""
    }

    function subheading() {
        if (isEpisode) return (item.episodeLabel ? item.episodeLabel + "  " : "") + (item.name || "")
        if (isSeason) return item.name || ""
        return item.year ? String(item.year) : ""
    }

    Image {
        id: logo
        anchors.left: parent.left
        anchors.top: parent.top
        width: Theme.px(560)
        height: Theme.px(170)
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignLeft
        asynchronous: true
        source: pane.item.logo || ""
        sourceSize.width: Theme.px(800)
        visible: status === Image.Ready
    }

    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: logo.bottom
        visible: !logo.visible
        text: pane.heading()
        elide: Text.ElideRight
        font.family: Theme.fontFamily
        font.capitalization: Theme.caps
        font.pixelSize: Theme.px(64)
        font.bold: true
        color: Theme.text
    }

    Image {
        id: poster
        anchors.left: parent.left
        anchors.top: logo.bottom
        anchors.topMargin: Theme.px(40)
        width: Theme.px(400)
        height: Theme.px(600)
        fillMode: Image.PreserveAspectFit
        verticalAlignment: Image.AlignTop
        asynchronous: true
        source: pane.item.poster || ""
        sourceSize.height: Theme.px(600)
    }

    Column {
        anchors.left: poster.right
        anchors.leftMargin: Theme.px(40)
        anchors.right: parent.right
        anchors.top: poster.top
        spacing: Theme.px(14)

        Text {
            width: parent.width
            text: pane.subheading()
            visible: text !== ""
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.capitalization: Theme.caps
            font.pixelSize: Theme.px(36)
            font.bold: true
            color: Theme.text
        }

        Text {
            width: parent.width
            text: pane.metaLine()
            visible: text !== ""
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily
            font.capitalization: Theme.caps
            font.pixelSize: Theme.bodyFont
            color: Theme.highlight
        }

        Text {
            width: parent.width
            text: pane.item.tagline || ""
            visible: text !== ""
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            font.italic: true
            color: Theme.dim
        }

        // The plot scrolls slowly when it doesn't fit, as in Amber.
        Item {
            id: plotBox
            width: parent.width
            height: Theme.px(290)
            clip: true
            Text {
                id: plot
                width: parent.width
                text: pane.item.overview || ""
                wrapMode: Text.WordWrap
                lineHeight: 1.15
                font.family: Theme.fontFamily
                font.pixelSize: Theme.bodyFont
                color: Theme.text
                y: 0
                SequentialAnimation on y {
                    id: scroll
                    running: plot.implicitHeight > plotBox.height && pane.visible
                    loops: Animation.Infinite
                    PauseAnimation { duration: 6000 }
                    NumberAnimation {
                        to: plotBox.height - plot.implicitHeight
                        duration: Math.max(1, (plot.implicitHeight - plotBox.height) * 60)
                    }
                    PauseAnimation { duration: 4000 }
                    NumberAnimation { to: 0; duration: 600 }
                }
            }
            Connections {
                target: pane
                function onItemChanged() { scroll.stop(); plot.y = 0; if (plot.implicitHeight > plotBox.height) scroll.start() }
            }
        }

        Text {
            width: parent.width
            text: pane.item.genres || ""
            visible: text !== ""
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.capitalization: Theme.caps
            font.pixelSize: Theme.smallFont
            color: Theme.dim
        }

        Text {
            width: parent.width
            text: [pane.item.videoFlags, pane.item.audioFlags, pane.item.hasSubtitles ? "CC" : ""]
                  .filter(s => s).join("   |   ")
            visible: text !== ""
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.capitalization: Theme.caps
            font.pixelSize: Theme.smallFont
            font.bold: true
            color: Theme.dim
        }
    }
}
