// « Supprimer Inès ? » — what deleting a profile erases, and what it leaves.
//
// Asked before the request because nothing brings a profile back: its reading progress and
// its preferences go with it. The volumes stay, and the devices that kept it ask who reads
// rather than opening on the principal — which on a child's tablet would hand it the
// principal's profile.

import QtQuick
import QtQuick.Controls
import Leaf

Popup {
    id: removal

    objectName: "profile-removal"

    /// The profile to delete, from `Profiles.list`: never the principal, never one's own.
    property var profile: ({})
    property bool waiting: false
    property string said: ""

    function ask(target) {
        removal.profile = target
        removal.open()
    }

    width: 400
    anchors.centerIn: Overlay.overlay
    padding: 0
    modal: true
    focus: true
    closePolicy: removal.waiting ? Popup.NoAutoClose : Popup.CloseOnEscape
    onClosed: {
        removal.waiting = false
        removal.said = ""
    }

    Connections {
        target: Profiles

        function onChanged() {
            if (!removal.waiting || Profiles.busy)
                return
            removal.waiting = false
            removal.said = Profiles.trouble
            if (removal.said.length === 0)
                removal.close()
        }
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(12 / 255, 16 / 255, 14 / 255, 0.74)
    }

    background: Rectangle {
        radius: Theme.cardRadius
        color: Theme.surface
        border.color: Theme.rule
        border.width: 1
        antialiasing: true

        CardLift { level: CardLift.Modal }
    }

    contentItem: Item {
        objectName: "profile-removal-body"
        implicitHeight: form.height + 34 + actions.height + 18

        Column {
            id: form

            x: 20
            y: 18
            width: parent.width - 40
            spacing: 12

            Text {
                objectName: "profile-removal-title"
                width: parent.width
                text: Profiles.sayEraseProfile(removal.profile.name ?? "")
                // The name is the profile's own choice, shown as typed, never as markup.
                textFormat: Text.PlainText
                color: Theme.ink
                font.family: Theme.displayFamily
                font.pixelSize: 22
                font.weight: Font.Bold
                wrapMode: Text.Wrap
            }

            Text {
                width: parent.width
                text: Profiles.words.eraseConsequence + " " + Profiles.words.eraseLeavesDevices
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: 13
                lineHeight: 1.35
                wrapMode: Text.Wrap
            }

            Text {
                objectName: "profile-removal-trouble"
                width: parent.width
                visible: removal.said.length > 0
                text: removal.said
                color: Theme.alert
                font.family: Theme.textFamily
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
        }

        Row {
            id: actions

            anchors.right: form.right
            anchors.top: form.bottom
            anchors.topMargin: 18
            spacing: 8

            LeafTextAction {
                objectName: "profile-removal-cancel"
                anchors.verticalCenter: parent.verticalCenter
                label: Profiles.words.cancel
                enabled: !removal.waiting
                onTriggered: removal.close()
            }

            ActionButton {
                objectName: "profile-removal-confirm"
                label: Profiles.sayEraseProfileButton(removal.profile.name ?? "")
                danger: true
                ready: !removal.waiting
                onTriggered: {
                    removal.said = ""
                    removal.waiting = true
                    Profiles.remove(removal.profile.id)
                }
            }
        }
    }
}
