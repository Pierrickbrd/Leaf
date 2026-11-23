// The one confirmation in this client that stands in front of something a scan will not undo.
//
// It says what goes, what it weighs, what is lost besides the files and what the collection
// looks like afterwards — and for a whole edition it asks for the name to be typed, because a
// button that can be clicked by reflex is not a protection. The list behind stays visible:
// one deletes something one can see, not a name in a box.

import QtQuick
import QtQuick.Controls
import Leaf

LeafDialog {
    id: confirmation

    objectName: "erase-dialog"
    surfaceName: "erase-surface"
    // The name to type when there is one, the way that loses nothing otherwise.
    initialFocus: Erasure.whole ? confirmField : cancelAction
    // Empty at every question: `Erasure.ask` forgets what was typed, and the field kept the
    // last answer typed beside a question it no longer answered.
    onAboutToShow: confirmField.clear()
    visible: Erasure.asking
    // Only when closed by Escape, with the question still asked. « Annuler » dismisses
    // first: the dialog then closed because `asking` went false, and dismissing again from
    // here changed `asking` inside its own binding — a binding loop on `visible` at every
    // cancel, measured on 23/10/2026.
    onOpened: Erasure.drawn(true)
    onClosed: {
        Erasure.drawn(false)
        if (Erasure.asking)
            Erasure.dismiss()
    }

    // The question at a title's size, the details at a caption's — and the warning one pixel
    // over them: at 12 like the rest, the sentence that stands between a reader and a loss was
    // the smallest on the screen (audit, 23/10/2026). A caption of 13 and not a body: as a
    // body it kept its 13 at 800 where the paragraphs round it fell to their 11, and stood
    // out of the dialog two sizes larger than everything it qualifies (23/10/2026).
    Text {
        objectName: "erase-question"
        width: parent.width
        textFormat: Text.PlainText
        text: Erasure.said.question ?? ""
        color: Theme.ink
        font.family: Theme.displayFamily
        font.pixelSize: Widths.scale.title(20)
        font.weight: Font.Bold
        wrapMode: Text.WordWrap
    }

    // What goes and which file it is are one paragraph in two lines, not two blocks: they
    // are one fact about one thing.
    Column {
        width: parent.width
        spacing: Widths.space.tight

        Text {
            width: parent.width
            textFormat: Text.PlainText
            text: Erasure.said.what ?? ""
            visible: text.length > 0
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            wrapMode: Text.WordWrap
        }

        Text {
            width: parent.width
            textFormat: Text.PlainText
            text: Erasure.said.file ?? ""
            visible: text.length > 0
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            elide: Text.ElideMiddle
        }
    }

    Text {
        objectName: "erase-leaves"
        width: parent.width
        textFormat: Text.PlainText
        text: Erasure.said.leaves ?? ""
        visible: text.length > 0
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        lineHeight: 1.4
        wrapMode: Text.WordWrap
    }

    // Said plainly, in the colour of the two paragraphs above it. The design puts the
    // garnet on the button and nowhere else: a modal where the prose shouts as loudly as
    // the button is a modal where neither is read.
    Text {
        objectName: "erase-warning"
        width: parent.width
        textFormat: Text.PlainText
        text: Erasure.said.warning ?? ""
        visible: text.length > 0
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(13)
        lineHeight: 1.4
        wrapMode: Text.WordWrap
    }

    Text {
        width: parent.width
        textFormat: Text.PlainText
        text: Erasure.said.untouched ?? ""
        visible: text.length > 0
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        wrapMode: Text.WordWrap
    }

    // Only for a whole edition. Thirty files and nothing to come back to is worth the
    // weight; the same weight on one volume would be ceremony.
    Column {
        width: parent.width
        // Closer than two lines of the card: the sentence is the field's own caption.
        spacing: Widths.scale.room(6)
        visible: Erasure.whole

        Text {
            width: parent.width
            textFormat: Text.PlainText
            text: Erasure.said.confirm ?? ""
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            wrapMode: Text.WordWrap
        }

        LeafField {
            id: confirmField

            objectName: "erase-confirm"
            width: parent.width
            // The window opens on the name to type, when there is one.
            focus: Erasure.whole
            activeFocusOnTab: true
            onTextEdited: Erasure.typed(text)
            onAccepted: {
                if (Erasure.ready)
                    Erasure.go()
            }
        }
    }

    Text {
        objectName: "erase-trouble"
        width: parent.width
        textFormat: Text.PlainText
        text: Erasure.trouble
        visible: text.length > 0
        color: Theme.alert
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        wrapMode: Text.WordWrap
    }

    commands: [
        LeafTextAction {
            id: cancelAction

            objectName: "erase-cancel"
            scaled: true
            label: Erasure.cancelLabel
            // Otherwise on the way that erases nothing.
            focus: !Erasure.whole
            activeFocusOnTab: true
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: Erasure.dismiss()
        },

        ActionButton {
            objectName: "erase-go"
            scaled: true
            label: Erasure.eraseLabel
            danger: true
            ready: Erasure.ready
            onTriggered: Erasure.go()
        }
    ]
}
