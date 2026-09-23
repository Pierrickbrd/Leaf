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
    width: Math.min(460, Overlay.overlay ? Overlay.overlay.width - 80 : 460)
    anchors.centerIn: Overlay.overlay
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    visible: Erasure.asking
    onClosed: Erasure.dismiss()

    // Dimmed and not hidden: what is about to go is on the screen behind this.
    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, Theme.dark ? 0.62 : 0.38)
    }

    background: Rectangle {
        objectName: "erase-surface"
        radius: Theme.cardRadius
        color: Theme.paper
        border.color: Theme.rule
        border.width: 1
        antialiasing: true
    }

    Column {
        id: body

        x: 22
        y: 20
        width: parent.width - 44
        spacing: 12

        Text {
            objectName: "erase-question"
            width: parent.width
            text: Erasure.said.question ?? ""
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: 17
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            text: Erasure.said.what ?? ""
            visible: text.length > 0
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            text: Erasure.said.file ?? ""
            visible: text.length > 0
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: 12
            elide: Text.ElideMiddle
        }

        Text {
            objectName: "erase-leaves"
            width: parent.width
            text: Erasure.said.leaves ?? ""
            visible: text.length > 0
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 13
            lineHeight: 1.35
            wrapMode: Text.WordWrap
        }

        Text {
            objectName: "erase-warning"
            width: parent.width
            text: Erasure.said.warning ?? ""
            visible: text.length > 0
            color: Theme.alert
            font.family: Theme.textFamily
            font.pixelSize: 13
            lineHeight: 1.35
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            text: Erasure.said.untouched ?? ""
            visible: text.length > 0
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 13
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
        anchors.topMargin: 18
        anchors.bottomMargin: 20
        spacing: 10

        ActionButton {
            objectName: "erase-cancel"
            label: Erasure.cancelLabel
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
    implicitHeight: body.implicitHeight + 20 + 18 + 34 + 20
}
