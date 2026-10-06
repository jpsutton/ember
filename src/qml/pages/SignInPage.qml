// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// Sign in with Quick Connect (no typing) or a user name and password.
FocusScope {
    id: page

    property var app
    property string mode: "choose"   // choose, quick, user, password
    property string userName

    Component.onCompleted: {
        Session.clearError()
        if (Session.quickConnectAvailable) {
            mode = "quick"
            Session.startQuickConnect()
        }
    }
    Component.onDestruction: Session.cancelQuickConnect()

    // A refused password is cleared for the next try.
    Connections {
        target: Session
        function onErrorStringChanged() {
            if (Session.errorString !== "" && page.mode === "password") passwordEntry.text = ""
        }
    }

    function show(next) {
        mode = next
        if (next === "quick") Session.startQuickConnect()
        else Session.cancelQuickConnect()
        if (next === "user") { userEntry.text = userName; userEntry.forceActiveFocus() }
        else if (next === "password") { passwordEntry.text = ""; passwordEntry.forceActiveFocus() }
        else options.forceActiveFocus()
    }

    Rectangle { anchors.fill: parent; color: Theme.background }

    Column {
        x: Theme.px(160)
        y: Theme.px(120)
        width: Theme.px(1600)
        spacing: Theme.px(28)

        Text {
            text: qsTr("Sign in to %1").arg(Session.serverName || Session.serverUrl)
            font.family: Theme.fontFamily
            font.pixelSize: Theme.titleFont
            font.bold: true
            color: Theme.text
        }
        Text {
            visible: Session.errorString !== ""
            width: parent.width
            wrapMode: Text.WordWrap
            text: Session.errorString
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: Theme.error
        }

        Row {
            spacing: Theme.px(80)

            MenuList {
                id: options
                width: Theme.px(560)
                height: Theme.px(64) * 3
                rowHeight: Theme.px(64)
                fontSize: Theme.rowFont
                staticMode: true
                barHighlight: true
                horizontalAlignment: Text.AlignLeft
                dimmed: page.mode === "user" || page.mode === "password"
                focus: true
                model: [
                    { title: qsTr("Quick Connect") },
                    { title: qsTr("User name and password") },
                    { title: qsTr("Change server") },
                ]
                currentIndex: page.mode === "quick" ? 0 : 1
                onActivated: (index) => {
                    if (index === 0) page.show("quick")
                    else if (index === 1) page.show("user")
                    else Session.forgetServer()
                }
            }

            Column {
                visible: page.mode === "quick"
                spacing: Theme.px(20)
                width: Theme.px(900)
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("On a phone or computer signed in to Jellyfin, open your user settings, choose Quick Connect and enter this code:")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.bodyFont
                    color: Theme.text
                }
                Text {
                    text: Session.quickConnectCode || "…"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.px(120)
                    font.bold: true
                    font.letterSpacing: Theme.px(16)
                    color: Theme.highlight
                }
                Text {
                    visible: !Session.quickConnectAvailable
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Quick Connect looks turned off on this server; an administrator can enable it in the dashboard.")
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallFont
                    color: Theme.dim
                }
            }

            TextEntry {
                id: userEntry
                visible: page.mode === "user"
                width: implicitWidth
                label: qsTr("User name")
                onAccepted: { page.userName = text; page.show("password") }
                Keys.onPressed: (event) => { if (event.key === Qt.Key_Back) { page.show("choose"); event.accepted = true } }
            }

            TextEntry {
                id: passwordEntry
                visible: page.mode === "password"
                width: implicitWidth
                label: qsTr("Password for %1").arg(page.userName)
                password: true
                onAccepted: Session.signIn(page.userName, text)
                Keys.onPressed: (event) => { if (event.key === Qt.Key_Back) { page.show("user"); event.accepted = true } }
            }
        }
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Back) event.accepted = true
    }
}
