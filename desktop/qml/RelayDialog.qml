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

Popup {
    id: relaying

    /// « Copié » in place of « Copier » for a moment after a click, so that it reads as done.
    property bool copied: false

    objectName: "relay-dialog"
    width: Math.min(430, Overlay.overlay ? Overlay.overlay.width - 80 : 430)
    anchors.centerIn: Overlay.overlay
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    onOpened: Bond.relay()
    onClosed: Bond.stopRelay()
    // The settings may be torn down with the panel open — an unlinking elsewhere, a key
    // revoked — and then `closed` never comes.
    Component.onDestruction: Bond.stopRelay()

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

    Timer {
        id: settle

        interval: 1500
        onTriggered: relaying.copied = false
    }

    Column {
        id: body

        x: 17
        y: 16
        width: parent.width - 34
        spacing: 12

        Text {
            objectName: "relay-title"
            width: parent.width
            text: Bond.relayLabel
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: 20
            font.weight: Font.Bold
            wrapMode: Text.WordWrap
        }

        Text {
            objectName: "relay-intro"
            width: parent.width
            visible: Bond.relayLink.length > 0
            text: Bond.relayIntro
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 12
            lineHeight: 1.4
            wrapMode: Text.WordWrap
        }

        // The light square a camera reads, whatever the theme around it.
        Rectangle {
            objectName: "relay-qr"
            anchors.horizontalCenter: parent.horizontalCenter
            visible: Bond.relayLink.length > 0
            width: 212
            height: 212
            radius: 10
            color: "#F2F4F2"

            QrCode {
                anchors.centerIn: parent
                width: 204
                height: 204
                text: Bond.relayLink
            }
        }

        // Selectable, for a device the camera cannot reach: the link is copied by hand as
        // well as by the button.
        Rectangle {
            width: parent.width
            height: link.implicitHeight + 16
            visible: Bond.relayLink.length > 0
            radius: 7
            color: Theme.onBar

            TextEdit {
                id: link

                objectName: "relay-link"
                x: 10
                y: 8
                width: parent.width - 20
                text: Bond.relayLink
                readOnly: true
                selectByMouse: true
                selectionColor: Theme.emerald
                selectedTextColor: Theme.onEmerald
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: 11
                wrapMode: TextEdit.WrapAnywhere
                horizontalAlignment: TextEdit.AlignHCenter
            }
        }

        Text {
            objectName: "relay-time-left"
            width: parent.width
            visible: text.length > 0
            text: Bond.relayTimeLeft
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
        }

        // « PC du salon relié »: the code was used, and the panel has nothing left to show.
        Row {
            objectName: "relay-done"
            anchors.horizontalCenter: parent.horizontalCenter
            visible: Bond.relayed.length > 0
            spacing: 8

            Glyph {
                anchors.verticalCenter: parent.verticalCenter
                source: "assets/icons/check_circle.svg"
                side: 20
                tint: Theme.emerald
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Bond.relayedSaid
                color: Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: 14
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
    }

    Row {
        anchors.right: body.right
        anchors.top: body.bottom
        anchors.topMargin: 16
        spacing: 8

        LeafTextAction {
            objectName: "relay-close"
            label: Bond.closeLabel
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: relaying.close()
        }

        ActionButton {
            objectName: "relay-copy"
            visible: Bond.relayLink.length > 0
            label: relaying.copied ? Bond.copiedLabel : Bond.copyLabel
            onTriggered: {
                Bond.copyRelay()
                relaying.copied = true
                settle.restart()
            }

            Glyph {
                source: "assets/icons/content_copy.svg"
                side: 16
                tint: Theme.onEmerald
            }
        }

        // A code that ran out, or could not be made: another, without closing and opening.
        ActionButton {
            objectName: "relay-again"
            visible: Bond.relayLink.length === 0 && Bond.relayed.length === 0
                     && Bond.relaySentence.length > 0
            label: Bond.anotherLinkLabel
            onTriggered: Bond.relay()
        }
    }

    implicitHeight: body.implicitHeight + 16 + 16 + 32 + 16
}
