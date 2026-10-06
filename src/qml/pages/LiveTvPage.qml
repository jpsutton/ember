// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

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

    // Watching comes with live playback; until then OK only marks the channel.
    function watch(index) {
        const ch = channels[index]
        if (ch) playingId = ch.id
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
            if (target >= 0) page.setRow(target)
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
        }
    }

    // The page's background, around the preview so the video shows through.
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

    // The preview: black until there is a picture.
    Rectangle {
        id: preview
        x: parent.width - page.margin - page.previewWidth
        y: page.pageTop
        width: page.previewWidth
        height: page.previewHeight
        color: "black"
    }

    // Details of the focused channel and programme.
    Column {
        x: page.margin
        y: page.pageTop
        width: preview.x - page.margin - Theme.px(32)
        height: page.previewHeight
        spacing: Theme.px(10)
        visible: page.channels.length > 0

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
                        visible: rowItem.modelData.id === page.playingId
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

    Keys.onPressed: (event) => {
        if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9 || event.key === Qt.Key_Period) {
            page.digits = (page.digits + event.text).slice(-5)
            digitTimer.restart()
            event.accepted = true
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
