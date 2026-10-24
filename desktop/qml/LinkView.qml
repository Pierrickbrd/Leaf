// « Aucune bibliothèque reliée » — what the client opens on until it is linked to a library.
//
// One field for the link, and « Relier » greyed until there is something to link to. Once the
// library at the link's address has answered with the certificate the link names, the
// « Bibliothèque trouvée » block appears, with the device's name to confirm. Everything said
// here is decided in `Linking`; this file only places it.
//
// **No way past a refusal.** A server presenting another certificate gets its sentence and a
// button that stays grey: it is either a server that changed or somebody standing in front of
// it, and nobody reading a warning at that moment can tell which.

import QtQuick
import Leaf

FocusScope {
    id: view

    /// Whatever went wrong, as opposed to what is under way: the check in progress and the
    /// code on its way say something too, and are not alarms.
    readonly property bool alarming: Linking.stage !== Linking.Checking
                                     && Linking.stage !== Linking.Sending
    readonly property bool found: Linking.found.length > 0

    // Read once, as the screen opens: a link already copied fills the field by itself.
    Component.onCompleted: {
        Linking.lookAtTheClipboard()
        linkField.forceActiveFocus()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.paper
    }

    // Nothing behind this screen answers a click: the shelf under it has nothing to show.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onPressed: mouse => mouse.accepted = true
        onWheel: wheel => wheel.accepted = true
    }

    // The bar, with only the name: none of the shelf's commands mean anything yet.
    Rectangle {
        id: bar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 58
        color: Theme.surface

        Image {
            id: mark

            anchors.left: parent.left
            anchors.leftMargin: Widths.shelfMargin
            anchors.verticalCenter: parent.verticalCenter
            width: 36
            height: 36
            source: Theme.dark
                    ? "assets/logos/leaf-mark-dark.svg"
                    : "assets/logos/leaf-mark-light.svg"
            sourceSize: Qt.size(36, 36)
        }

        Text {
            anchors.left: mark.right
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: "Leaf"
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: 25
            font.weight: Font.Bold
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.rule
            opacity: Theme.dark ? 0.7 : 0.45
        }
    }

    Column {
        id: block

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: bar.height / 2
        width: Math.min(420, view.width - 2 * Widths.shelfMargin)
        spacing: 14

        Glyph {
            visible: !view.found
            source: "assets/icons/cloud_off.svg"
            side: 34
            tint: Theme.emerald
        }

        Text {
            objectName: "link-title"
            width: parent.width
            text: Linking.title
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: 30
            font.weight: Font.Bold
            wrapMode: Text.Wrap
        }

        Text {
            width: parent.width
            visible: !view.found
            text: Linking.intro
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 14
            lineHeight: 1.3
            wrapMode: Text.Wrap
        }

        Rectangle {
            id: form

            readonly property int padding: 16

            width: parent.width
            height: fields.height + 2 * padding
            radius: Theme.cardRadius
            color: Theme.surface
            antialiasing: true

            CardLift { level: CardLift.Card }

            Column {
                id: fields

                x: form.padding
                y: form.padding
                width: form.width - 2 * form.padding
                spacing: 12

                Column {
                    width: parent.width
                    spacing: 6

                    Text {
                        text: Linking.linkField
                        color: Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: 13
                        font.weight: Font.Medium
                    }

                    LeafField {
                        id: linkField

                        objectName: "link-field"
                        width: parent.width
                        activeFocusOnTab: true
                        // A code on its way may not be swapped for another link: its answer is
                        // a key, and the key belongs to this link.
                        enabled: Linking.stage !== Linking.Sending
                        text: Linking.typed
                        placeholderText: Linking.linkPlaceholder
                        onTextEdited: Linking.offer(text)
                        // Enter links, as it does from the name below: a link pasted and
                        // checked is a link ready to use, and Enter here did nothing.
                        onAccepted: Linking.link()
                    }
                }

                // What the check found: the address, the certificate the link named, and how
                // much is there. Kept while a taken name is being changed, since the library
                // has not gone anywhere.
                Rectangle {
                    objectName: "link-found"
                    width: parent.width
                    visible: view.found
                    height: foundWords.height + 18
                    radius: 9
                    color: Theme.emeraldWash

                    Glyph {
                        id: foundGlyph

                        x: 12
                        anchors.verticalCenter: parent.verticalCenter
                        source: "assets/icons/check_circle.svg"
                        side: 20
                        tint: Theme.emerald
                    }

                    Column {
                        id: foundWords

                        anchors.left: foundGlyph.right
                        anchors.leftMargin: 10
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        Text {
                            width: parent.width
                            text: Linking.found
                            color: Theme.ink
                            font.family: Theme.textFamily
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            wrapMode: Text.Wrap
                        }

                        Text {
                            width: parent.width
                            text: Linking.detail
                            color: Theme.inkSoft
                            font.family: Theme.textFamily
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                        }
                    }
                }

                Rectangle {
                    objectName: "link-sentence"
                    width: parent.width
                    visible: Linking.sentence.length > 0
                    height: sentence.height + 18
                    radius: 9
                    color: view.alarming ? Theme.alertWash : Theme.onBar

                    Glyph {
                        id: sentenceGlyph

                        x: 12
                        y: 10
                        visible: view.alarming
                        source: "assets/icons/error.svg"
                        side: 18
                        tint: Theme.alert
                    }

                    Text {
                        id: sentence

                        anchors.left: view.alarming ? sentenceGlyph.right : parent.left
                        anchors.leftMargin: view.alarming ? 10 : 12
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: Linking.sentence
                        color: Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: 13
                        lineHeight: 1.25
                        wrapMode: Text.Wrap
                    }
                }

                Column {
                    width: parent.width
                    visible: view.found
                    spacing: 6

                    Text {
                        text: Linking.nameField
                        color: Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: 13
                        font.weight: Font.Medium
                    }

                    LeafField {
                        objectName: "device-name-field"
                        width: parent.width
                        activeFocusOnTab: true
                        enabled: Linking.stage !== Linking.Sending
                        text: Linking.name
                        onTextEdited: Linking.name = text
                        onAccepted: Linking.link()
                    }

                    Text {
                        width: parent.width
                        text: Linking.nameHelp
                        color: Theme.inkSoft
                        font.family: Theme.textFamily
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }
                }

                ActionButton {
                    objectName: "link-action"
                    anchors.right: parent.right
                    label: Linking.action
                    ready: Linking.canLink
                    onTriggered: Linking.link()
                }
            }
        }
    }
}
