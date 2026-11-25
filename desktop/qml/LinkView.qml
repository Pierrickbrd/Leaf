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
        height: Widths.scale.size(58)
        color: Theme.surface

        // The application bar's name, and not a copy of it: this one had kept its 36, its 10
        // and its 25 by hand (spacing audit, 23/10/2026).
        BarBrand {
            anchors.left: parent.left
            anchors.leftMargin: Widths.space.gutter
            anchors.verticalCenter: parent.verticalCenter
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
        width: Math.min(420, view.width - 2 * Widths.space.gutter)
        // Glyph, headline, sentence and form are blocks of one column, the series page's
        // gap between its blocks. It was a literal 14, which is that gap at 1280 and at no
        // other window (spacing audit, 23/10/2026).
        spacing: Widths.space.block

        Glyph {
            visible: !view.found
            source: "assets/icons/cloud_off.svg"
            side: 34
            tint: Theme.emerald
        }

        Text {
            objectName: "link-title"
            width: parent.width
            textFormat: Text.PlainText
            text: Linking.title
            color: Theme.ink
            font.family: Theme.displayFamily
            // The one thing on the screen, and one of the three words allowed to grow with
            // the window (audit, 23/10/2026): 30 pixels in the middle of 1920 read as a
            // caption. It shrinks with every other word below 1280.
            font.pixelSize: Widths.scale.display(30)
            font.weight: Font.Bold
            wrapMode: Text.Wrap
        }

        Text {
            width: parent.width
            visible: !view.found
            textFormat: Text.PlainText
            text: Linking.intro
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 14
            lineHeight: 1.3
            wrapMode: Text.Wrap
        }

        // The settings' card and not a surface of its own: the two were one card drawn twice,
        // this copy 12 between its lines where the settings left 10 (spacing audit,
        // 23/10/2026).
        Card {
            id: form

            width: parent.width

            Column {
                width: parent.width
                spacing: 6

                Text {
                    textFormat: Text.PlainText
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
                    onAccepted: Linking.canLook ? Linking.look() : Linking.link()
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
                        textFormat: Text.PlainText
                        text: Linking.found
                        color: Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: 14
                        font.weight: Font.Medium
                        wrapMode: Text.Wrap
                    }

                    Text {
                        width: parent.width
                        textFormat: Text.PlainText
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
                    textFormat: Text.PlainText
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
                    textFormat: Text.PlainText
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
                    textFormat: Text.PlainText
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
                ready: Linking.canLink || Linking.canLook
                onTriggered: Linking.canLook ? Linking.look() : Linking.link()
            }
        }
    }
}
