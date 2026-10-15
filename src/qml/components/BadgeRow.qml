// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import Ember

// A row of MediaBadges that wraps, from a list of {tag, icon, value}.
Flow {
    id: flow

    property var badges: []

    spacing: Theme.px(14)
    visible: badges.length > 0

    Repeater {
        model: flow.badges
        MediaBadge {
            required property var modelData
            tag: modelData.tag || ""
            icon: modelData.icon || ""
            value: modelData.value || ""
        }
    }
}
