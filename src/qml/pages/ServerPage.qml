// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// First run: pick a server found on the network, or type its address.
FocusScope {
    id: page

    property var app

    readonly property var choices: {
        const list = Session.discoveredServers.map(s => ({ title: s.name, detail: s.address,
                                                            address: s.address + "|" + s.fallback }))
        list.push({ title: qsTr("Enter an address…"), detail: "", address: "" })
        return list
    }

    Component.onCompleted: Session.discover()

    Rectangle { anchors.fill: parent; color: Theme.background }

    Column {
        x: Theme.px(160)
        y: Theme.px(140)
        width: Theme.px(1000)
        spacing: Theme.px(30)

        Text {
            text: "Ember"
            font.family: Theme.menuFontFamily
            font.pixelSize: Theme.px(120)
            color: Theme.highlight
        }
        Text {
            text: entry.visible ? qsTr("Server address, e.g. 192.168.1.10 or jellyfin.example.com")
                                : qsTr("Choose your Jellyfin server.")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.rowFont
            color: Theme.text
        }
        Text {
            visible: Session.discovering
            text: qsTr("Looking for servers…")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: Theme.dim
        }
        Text {
            visible: Session.busy
            text: qsTr("Connecting…")
            font.family: Theme.fontFamily
            font.pixelSize: Theme.bodyFont
            color: Theme.dim
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

        MenuList {
            id: servers
            visible: !entry.visible
            width: Theme.px(900)
            height: Theme.px(64) * Math.max(1, page.choices.length)
            rowHeight: Theme.px(64)
            fontSize: Theme.rowFont
            staticMode: true
            barHighlight: true
            horizontalAlignment: Text.AlignLeft
            dimmed: false
            focus: true
            model: page.choices
            onActivated: (index) => {
                const choice = page.choices[index]
                if (choice.address === "") {
                    entry.visible = true
                    entry.forceActiveFocus()
                } else {
                    Session.connectToServer(choice.address)
                }
            }
        }

        TextEntry {
            id: entry
            visible: false
            width: implicitWidth
            label: qsTr("Address")
            placeholder: "http://"
            onAccepted: Session.connectToServer(text)
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Back) {
                    entry.visible = false
                    servers.forceActiveFocus()
                    event.accepted = true
                }
            }
        }
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Back) event.accepted = true  // nowhere to go back to
    }
}
