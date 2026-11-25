// « Relier un autre appareil », after the design: a QR code, the link in clear, the time it
// has left, and « Copier ». No « Partager »: Qt has no system share on Linux, nor the xdg
// portal — that button is Android's.
//
// The code is asked for when the panel opens and withdrawn when it closes, whichever way it
// closes: a link nobody is looking at any more is a key waiting for whoever finds it. The
// QR and the text show only while the code is good — gone once used, once run out.

import QtQuick
import QtQuick.Controls
import Leaf

LeafDialog {
    id: relaying

    /// « Copié » in place of « Copier » for a moment after a click, so that it reads as done.
    property bool copied: false

    objectName: "relay-dialog"
    initialFocus: closeAction

    onOpened: Bond.relay()
    onClosed: Bond.stopRelay()
    // The settings may be torn down with the panel open — an unlinking elsewhere, a key
    // revoked — and then `closed` never comes.
    Component.onDestruction: Bond.stopRelay()

    Timer {
        id: settle

        interval: 1500
        onTriggered: relaying.copied = false
    }

    Text {
        objectName: "relay-title"
        width: parent.width
        textFormat: Text.PlainText
        text: Bond.relayLabel
        color: Theme.ink
        font.family: Theme.displayFamily
        font.pixelSize: Widths.scale.title(20)
        font.weight: Font.Bold
        wrapMode: Text.WordWrap
    }

    Text {
        objectName: "relay-intro"
        width: parent.width
        visible: Bond.relayLink.length > 0
        textFormat: Text.PlainText
        text: Bond.relayIntro
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        lineHeight: 1.4
        wrapMode: Text.WordWrap
    }

    // The light square a camera reads, whatever the theme around it. A length and not a
    // column: a camera reads modules, not words, and the desktop's text size says nothing
    // about how far away the telephone is held.
    Rectangle {
        objectName: "relay-qr"
        anchors.horizontalCenter: parent.horizontalCenter
        visible: Bond.relayLink.length > 0
        width: Math.min(Widths.scale.size(212), parent.width)
        height: width
        radius: 10
        color: "#F2F4F2"

        QrCode {
            anchors.centerIn: parent
            width: parent.width - Widths.scale.size(8)
            height: width
            text: Bond.relayLink
        }
    }

    // Selectable, for a device the camera cannot reach: the link is copied by hand as
    // well as by the button.
    Rectangle {
        width: parent.width
        height: link.implicitHeight + 2 * Widths.space.compact
        visible: Bond.relayLink.length > 0
        radius: 7
        color: Theme.onBar

        TextEdit {
            id: link

            objectName: "relay-link"
            x: Widths.space.row
            y: Widths.space.compact
            width: parent.width - 2 * Widths.space.row
            textFormat: Text.PlainText
            text: Bond.relayLink
            readOnly: true
            selectByMouse: true
            selectionColor: Theme.emerald
            selectedTextColor: Theme.onEmerald
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(11)
            wrapMode: TextEdit.WrapAnywhere
            horizontalAlignment: TextEdit.AlignHCenter
        }
    }

    Text {
        objectName: "relay-time-left"
        width: parent.width
        visible: text.length > 0
        textFormat: Text.PlainText
        text: Bond.relayTimeLeft
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(11)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    // « PC du salon relié »: the code was used, and the panel has nothing left to show.
    Row {
        objectName: "relay-done"
        anchors.horizontalCenter: parent.horizontalCenter
        visible: Bond.relayed.length > 0
        spacing: Widths.space.label

        Glyph {
            anchors.verticalCenter: parent.verticalCenter
            source: "assets/icons/check_circle.svg"
            side: Widths.scale.icon(20)
            tint: Theme.emerald
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: Bond.relayedSaid
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(14)
            font.weight: Font.Medium
        }
    }

    // A link to one of the server's network addresses works from that network only.
    SettingsNote {
        objectName: "relay-same-network"
        width: parent.width
        visible: Bond.relaySameNetwork.length > 0
        text: Bond.relaySameNetwork
    }

    SettingsNote {
        objectName: "relay-sentence"
        width: parent.width
        visible: Bond.relaySentence.length > 0
        text: Bond.relaySentence
        alarming: true
    }

    commands: [
        LeafTextAction {
            id: closeAction

            objectName: "relay-close"
            scaled: true
            label: Bond.closeLabel
            // Always there, where « Copier » waits for the code: the window opens on it.
            focus: true
            activeFocusOnTab: true
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: relaying.close()
        },

        ActionButton {
            objectName: "relay-copy"
            scaled: true
            visible: Bond.relayLink.length > 0
            label: relaying.copied ? Bond.copiedLabel : Bond.copyLabel
            onTriggered: {
                Bond.copyRelay()
                relaying.copied = true
                settle.restart()
            }

            Glyph {
                source: "assets/icons/content_copy.svg"
                side: Widths.scale.icon(16)
                tint: Theme.onEmerald
            }
        },

        // A code that ran out, or could not be made: another, without closing and opening.
        ActionButton {
            objectName: "relay-again"
            scaled: true
            visible: Bond.relayLink.length === 0 && Bond.relayed.length === 0
                     && Bond.relaySentence.length > 0
            label: Bond.anotherLinkLabel
            onTriggered: Bond.relay()
        }
    ]
}
