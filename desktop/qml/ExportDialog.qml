// What to do with the paths an export would write over.
//
// Asked before anything is written, with the three answers a folder that already holds some of
// it can mean: replace what is there, add only what is missing, or think again.

import QtQuick
import Leaf

LeafDialog {
    id: dialog

    objectName: "export-dialog"
    surfaceName: "export-surface"
    initialFocus: later
    visible: Exports.asking
    // Escape is « Annuler » too: the export was waiting on this answer, and a question closed
    // with no answer would leave it waiting for good.
    onClosed: {
        if (Exports.asking)
            Exports.cancel()
    }

    Text {
        width: parent.width
        textFormat: Text.PlainText
        text: Exports.words.conflictTitle
        color: Theme.ink
        font.family: Theme.displayFamily
        font.pixelSize: Widths.scale.title(20)
        font.weight: Font.Bold
        wrapMode: Text.WordWrap
    }

    Text {
        objectName: "export-question"
        width: parent.width
        textFormat: Text.PlainText
        text: Exports.question
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(13)
        lineHeight: 1.4
        wrapMode: Text.WordWrap
    }

    commands: [
        LeafTextAction {
            id: later

            objectName: "export-cancel"
            scaled: true
            label: Exports.words.cancel
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: Exports.cancel()
        },

        LeafTextAction {
            objectName: "export-complete"
            scaled: true
            label: Exports.words.complete
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: Exports.complete()
        },

        ActionButton {
            objectName: "export-replace"
            scaled: true
            label: Exports.words.replace
            onTriggered: Exports.replace()
        }
    ]
}
