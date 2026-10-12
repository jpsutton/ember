// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// Settings: OK cycles a value or opens a choice.
FocusScope {
    id: page

    property var app

    function cycle(values, current) {
        const index = values.indexOf(current)
        return values[(index + 1) % values.length]
    }

    function onOff(value) { return value ? qsTr("On") : qsTr("Off") }

    readonly property var subtitleModes: ({ server: qsTr("As the server says"), off: qsTr("Off"),
                                            forced: qsTr("Forced only"), always: qsTr("Always") })

    // Codes are ISO 639-2 (what Jellyfin reports) plus an optional region.
    readonly property var languages: [
        { code: "", name: qsTr("As the server says") },
        { code: "eng-US", name: qsTr("English (US)") },
        { code: "eng-GB", name: qsTr("English (UK)") },
        { code: "eng", name: qsTr("English (any)") },
        { code: "spa", name: qsTr("Spanish") },
        { code: "fre", name: qsTr("French") },
        { code: "ger", name: qsTr("German") },
        { code: "ita", name: qsTr("Italian") },
        { code: "por", name: qsTr("Portuguese") },
        { code: "jpn", name: qsTr("Japanese") },
        { code: "kor", name: qsTr("Korean") },
        { code: "chi", name: qsTr("Chinese") },
    ]

    function languageName(code) {
        const entry = languages.find(l => l.code === code)
        return entry ? entry.name : code
    }

    function pickLanguage(title, current, apply) {
        const row = list.currentIndex  // the rows rebuild after the change
        page.app.menu.open(title, languages.map(l => ({ title: l.name, checked: l.code === current })),
                           (i) => { apply(languages[i].code); EmberSettings.save(); list.currentIndex = row })
    }

    readonly property var rows: [
        { title: qsTr("Libraries on the home menu"), detail: "", action: () => page.app.stack.push(page.app.libraryPickerComponent) },
        { title: qsTr("Interface size"), detail: Math.round(EmberSettings.uiScale * 100) + " %",
          action: () => { EmberSettings.uiScale = cycle([1.0, 1.1, 1.2, 1.3, 1.5, 0.8, 0.9], Math.round(EmberSettings.uiScale * 10) / 10); EmberSettings.save() } },
        { title: qsTr("Highlight color"), detail: Theme.accentEntry.name,
          action: () => { EmberSettings.accent = cycle(Theme.accents.map(a => a.id), Theme.accentEntry.id); EmberSettings.save() } },
        { title: qsTr("Home menu scrolls"), detail: EmberSettings.staticMenu ? qsTr("No, the highlight moves") : qsTr("Yes, the highlight stays"),
          action: () => { EmberSettings.staticMenu = !EmberSettings.staticMenu; EmberSettings.save() } },
        { title: qsTr("Keep list highlight centered"), detail: onOff(EmberSettings.centeredListFocus),
          action: () => { EmberSettings.centeredListFocus = !EmberSettings.centeredListFocus; EmberSettings.save() } },
        { title: qsTr("Mix surround to stereo"), detail: onOff(EmberSettings.downmix),
          action: () => { EmberSettings.downmix = !EmberSettings.downmix; EmberSettings.save() } },
        { title: qsTr("Dialogue boost in the stereo mix"), detail: EmberSettings.centerBoostDb + " dB",
          action: () => { EmberSettings.centerBoostDb = cycle([0, 3, 6, 8, 10, 12], EmberSettings.centerBoostDb); EmberSettings.save() } },
        { title: qsTr("Streaming quality"), detail: EmberSettings.maxBitrateMbps === 0 ? qsTr("Original") : EmberSettings.maxBitrateMbps + " Mbit/s",
          action: () => { EmberSettings.maxBitrateMbps = cycle([0, 40, 20, 10, 8, 4, 2], EmberSettings.maxBitrateMbps); EmberSettings.save() } },
        { title: qsTr("Audio language"), detail: languageName(EmberSettings.audioLanguage),
          action: () => pickLanguage(qsTr("Audio language"), EmberSettings.audioLanguage, (code) => EmberSettings.audioLanguage = code) },
        { title: qsTr("Subtitles"), detail: subtitleModes[EmberSettings.subtitleMode] || EmberSettings.subtitleMode,
          action: () => { EmberSettings.subtitleMode = cycle(["server", "off", "forced", "always"], EmberSettings.subtitleMode); EmberSettings.save() } },
        { title: qsTr("Subtitle language"), detail: languageName(EmberSettings.subtitleLanguage),
          action: () => pickLanguage(qsTr("Subtitle language"), EmberSettings.subtitleLanguage, (code) => EmberSettings.subtitleLanguage = code) },
        { title: qsTr("Play the next episode automatically"), detail: onOff(EmberSettings.autoPlayNext),
          action: () => { EmberSettings.autoPlayNext = !EmberSettings.autoPlayNext; EmberSettings.save() } },
        { title: qsTr("Next episode countdown"), detail: EmberSettings.nextUpSeconds + " s",
          action: () => { EmberSettings.nextUpSeconds = cycle([5, 10, 15, 20, 30], EmberSettings.nextUpSeconds); EmberSettings.save() } },
        { title: qsTr("Skip intros automatically"), detail: onOff(EmberSettings.autoSkipIntro),
          action: () => { EmberSettings.autoSkipIntro = !EmberSettings.autoSkipIntro; EmberSettings.save() } },
        { title: qsTr("Left and Right seek"), detail: EmberSettings.seekStepSeconds + " s",
          action: () => { EmberSettings.seekStepSeconds = cycle([5, 10, 15, 30, 60], EmberSettings.seekStepSeconds); EmberSettings.save() } },
        { title: qsTr("Sign out"), detail: Session.userName, action: () => Session.signOut() },
        { title: qsTr("Server"), detail: Session.serverName + " " + Session.serverVersion, action: () => page.app.switchServer() },
    ]

    Rectangle { anchors.fill: parent; color: Theme.background }

    Text {
        id: heading
        x: Theme.px(160)
        y: Theme.px(90)
        text: qsTr("Settings")
        font.family: Theme.fontFamily
        font.pixelSize: Theme.titleFont
        font.bold: true
        color: Theme.text
    }

    ListView {
        id: list
        x: Theme.px(160)
        anchors.top: heading.bottom
        anchors.topMargin: Theme.px(40)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.px(80)
        width: Theme.px(1400)
        clip: true
        focus: true
        keyNavigationWraps: true
        highlightMoveDuration: 0
        model: page.rows
        delegate: Item {
            id: row
            required property int index
            required property var modelData
            readonly property bool current: ListView.isCurrentItem
            width: list.width
            height: Theme.px(68)
            Rectangle { anchors.fill: parent; color: Theme.highlight; visible: row.current }
            Text {
                x: Theme.px(24)
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.title
                font.family: Theme.fontFamily
                font.pixelSize: Theme.rowFont
                font.bold: row.current
                color: row.current ? Theme.highlightText : Theme.text
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: Theme.px(24)
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.detail
                font.family: Theme.fontFamily
                font.pixelSize: Theme.bodyFont
                color: row.current ? Theme.highlightText : Theme.highlight
            }
        }
        Keys.onReturnPressed: {
            const index = currentIndex
            page.rows[index].action()
            currentIndex = index
        }
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: Theme.px(80)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.px(40)
        text: "Ember " + Qt.application.version
        font.family: Theme.fontFamily
        font.pixelSize: Theme.smallFont
        color: Theme.faint
    }
}
