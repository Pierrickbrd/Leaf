// Réglages › À propos: which Leaf this is, which server it talks to, and the keys it answers.
//
// Nothing else yet. Checking for an update and uninstalling come with the work that makes
// them do something; a button that does nothing is worse than no button.

import QtQuick
import Leaf

Item {
    id: section

    /// The settings screen's two columns, handed down — see `ProfilesSection`.
    property real side: width
    property real gap: 28
    property bool sideBySide: false

    height: sideBySide ? Math.max(left.height, right.height) : left.height + gap + right.height

    Column {
        id: left

        width: section.side
        spacing: section.gap

        SettingsCard {
            objectName: "settings-about"
            title: Qt.application.name

            SettingsLine {
                objectName: "about-client"
                width: parent.width
                label: Profiles.words.thisClient
                value: Qt.application.version
            }

            SettingsLine {
                objectName: "about-server"
                width: parent.width
                visible: Health.reachable
                label: Profiles.words.serverHeading
                value: Health.versions
            }
        }
    }

    Column {
        id: right

        x: section.sideBySide ? section.side + section.gap : 0
        y: section.sideBySide ? 0 : left.height + section.gap
        width: section.width - x
        spacing: section.gap

        SettingsCard {
            objectName: "settings-shortcuts"
            title: Profiles.words.shortcuts

            Repeater {
                model: Profiles.words.shortcutList

                delegate: SettingsLine {
                    required property var modelData

                    width: parent.width
                    label: modelData.what
                    value: modelData.keys
                }
            }
        }
    }
}
