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

        // Two cards where there was one: what runs here, then what answers there. One list
        // of two lines said a version twice and nothing else — no address, no system, and
        // « API 1 · format 1 » for numbers nobody could tell apart.
        SettingsCard {
            objectName: "settings-about"
            title: Qt.application.name

            SettingsLine {
                objectName: "about-client"
                width: parent.width
                label: Profiles.words.labels.version
                value: Qt.application.version
                strong: true
            }

            SettingsLine {
                objectName: "about-device"
                width: parent.width
                visible: Settings.deviceName.length > 0
                label: Profiles.words.labels.device
                value: Settings.deviceName
            }

            Repeater {
                model: Profiles.words.clientFacts

                delegate: SettingsLine {
                    required property var modelData

                    width: parent.width
                    label: modelData.label
                    value: modelData.value
                }
            }
        }

        SettingsCard {
            objectName: "settings-about-server"
            title: Profiles.words.serverHeading

            SettingsLine {
                objectName: "about-state"
                width: parent.width
                label: Profiles.words.labels.state
                value: Health.answering
                strong: true
            }

            SettingsLine {
                objectName: "about-access"
                width: parent.width
                visible: Health.reachable
                label: Profiles.words.labels.access
                value: Health.access
            }

            SettingsLine {
                objectName: "about-api"
                width: parent.width
                visible: Health.reachable
                label: Profiles.words.labels.api
                value: String(Health.api)
            }

            SettingsLine {
                objectName: "about-format"
                width: parent.width
                visible: Health.reachable
                label: Profiles.words.labels.format
                value: String(Health.format)
            }

            SettingsNote {
                objectName: "about-api-explained"
                width: parent.width
                visible: Health.reachable
                text: Profiles.words.apiExplained
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

            // Said before the keys: « Échap » and the arrows are not what they do here once a
            // book is open, and a list that did not say so read as true in the reader too.
            SettingsNote {
                objectName: "shortcuts-scope"
                width: parent.width
                text: Profiles.words.shortcutsScope
            }

            Repeater {
                model: Profiles.words.shortcutList

                // The keys as the reader's card draws them, a size down, and the words on
                // the left: a list of « Échap » and « ← ↑ → ↓ ou Tab » in plain type was a
                // list to be decoded, and the reader already taught these shapes.
                delegate: Item {
                    id: line

                    required property var modelData

                    width: parent.width
                    height: Math.max(what.implicitHeight, row.implicitHeight) + 14

                    Text {
                        id: what

                        objectName: "shortcut-what"
                        anchors.left: parent.left
                        anchors.right: row.left
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: line.modelData.what
                        color: Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: 14
                        wrapMode: Text.Wrap
                    }

                    KeyRow {
                        id: row

                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        compact: true
                        keys: line.modelData.keys
                    }
                }
            }
        }
    }
}
