// The one confirmation in this client that stands in front of something a scan will not undo.
//
// It says what goes, what it weighs, what is lost besides the files and what the collection
// looks like afterwards — and for a whole edition it asks for the name to be typed, because a
// button that can be clicked by reflex is not a protection. The list behind stays visible:
// one deletes something one can see, not a name in a box.

import QtQuick
import QtQuick.Controls
import Leaf

Popup {
    id: confirmation

    objectName: "erase-dialog"
    width: Math.min(430, Overlay.overlay ? Overlay.overlay.width - 80 : 430)
    anchors.centerIn: Overlay.overlay
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    visible: Erasure.asking
    onClosed: Erasure.dismiss()

    // Dimmed and not hidden: what is about to go is on the screen behind this.
    // The design's own veil, and the same one the import dialog is laid under: what is about
    // to go stays visible behind it, because one deletes something one can see.
    Overlay.modal: Rectangle {
        color: Qt.rgba(12 / 255, 16 / 255, 14 / 255, 0.74)
    }

    background: Rectangle {
        objectName: "erase-surface"
        radius: Theme.cardRadius
        // A card, not the paper: a modal the colour of the page behind it has nothing but a
        // hairline saying where the page stops and the question starts.
        color: Theme.surface
        border.color: Theme.rule
        border.width: 1
        antialiasing: true

        CardLift { level: CardLift.Modal }
    }

    Column {
        id: body

        x: 17
        y: 16
        width: parent.width - 34
        spacing: 10

        Text {
            objectName: "erase-question"
            width: parent.width
            text: Erasure.said.question ?? ""
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: 20
            font.weight: Font.Bold
            wrapMode: Text.WordWrap
        }

        // What goes and which file it is are one paragraph in two lines, not two blocks: they
        // are one fact about one thing.
        Column {
            width: parent.width
            spacing: 1

            Text {
                width: parent.width
                text: Erasure.said.what ?? ""
                visible: text.length > 0
                color: Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: Erasure.said.file ?? ""
                visible: text.length > 0
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: 12
                elide: Text.ElideMiddle
            }
        }

        Text {
            objectName: "erase-leaves"
            width: parent.width
            text: Erasure.said.leaves ?? ""
            visible: text.length > 0
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 12
            lineHeight: 1.4
            wrapMode: Text.WordWrap
        }

        // Said plainly, in the colour of the two paragraphs above it. The design puts the
        // garnet on the button and nowhere else: a modal where the prose shouts as loudly as
        // the button is a modal where neither is read.
        Text {
            objectName: "erase-warning"
            width: parent.width
            text: Erasure.said.warning ?? ""
            visible: text.length > 0
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 12
            lineHeight: 1.4
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            text: Erasure.said.untouched ?? ""
            visible: text.length > 0
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        // Only for a whole edition. Thirty files and nothing to come back to is worth the
        // weight; the same weight on one volume would be ceremony.
        Column {
            width: parent.width
            spacing: 6
            visible: Erasure.whole

            Text {
                text: Erasure.said.confirm ?? ""
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: 12
            }

            LeafField {
                objectName: "erase-confirm"
                width: parent.width
                onTextEdited: Erasure.typed(text)
            }
        }

        Text {
            objectName: "erase-trouble"
            width: parent.width
            text: Erasure.trouble
            visible: text.length > 0
            color: Theme.alert
            font.family: Theme.textFamily
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }

    Row {
        anchors.right: body.right
        anchors.top: body.bottom
        anchors.topMargin: 16
        anchors.bottomMargin: 16
        spacing: 8

        LeafTextAction {
            objectName: "erase-cancel"
            label: Erasure.cancelLabel
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: Erasure.dismiss()
        }

        ActionButton {
            objectName: "erase-go"
            label: Erasure.eraseLabel
            danger: true
            ready: Erasure.ready
            onTriggered: Erasure.go()
        }
    }

    // The popup grows with what it says, which is not the same height for a file and for an
    // edition — and not the same again once the server has refused one.
    implicitHeight: body.implicitHeight + 16 + 16 + 32 + 16
}
