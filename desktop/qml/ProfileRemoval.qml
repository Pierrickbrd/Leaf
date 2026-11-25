// « Supprimer Inès ? » — what deleting a profile erases, and what it leaves.
//
// Asked before the request because nothing brings a profile back: its reading progress and
// its preferences go with it. The volumes stay, and the devices that kept it ask who reads
// rather than opening on the principal — which on a child's tablet would hand it the
// principal's profile.

import QtQuick
import QtQuick.Controls
import Leaf

LeafDialog {
    id: removal

    objectName: "profile-removal"
    initialFocus: cancelAction

    /// The profile to delete, from `Profiles.list`: never the principal, never one's own.
    property var profile: ({})
    property bool waiting: false
    property string said: ""

    function ask(target) {
        removal.profile = target
        removal.open()
    }

    drawnWidth: 400
    lineSpacing: Widths.space.block
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
        textFormat: Text.PlainText
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
        textFormat: Text.PlainText
        text: removal.said
        color: Theme.alert
        font.family: Theme.textFamily
        font.pixelSize: 13
        wrapMode: Text.Wrap
    }

    commands: [
        LeafTextAction {
            id: cancelAction

            objectName: "profile-removal-cancel"
            anchors.verticalCenter: parent.verticalCenter
            // On the way that loses nothing: an Enter pressed out of habit keeps the
            // profile.
            focus: true
            activeFocusOnTab: true
            label: Profiles.words.cancel
            enabled: !removal.waiting
            onTriggered: removal.close()
        },

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
    ]
}
