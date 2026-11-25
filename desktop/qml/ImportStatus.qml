// What the whole drop is doing, in one line under the zone it was dropped on.
//
// The button that goes on is greyed for as long as the folders are read and their volumes
// hashed — minutes, on a real library — and nothing said so: a reader saw a button that did
// not answer and no reason. This is the reason, with how far it has come, and the word of the
// button that waits.

import QtQuick
import Leaf

Rectangle {
    id: status

    /// The sentence, from `DropCaptions.dropProgressLabel`.
    required property string words
    /// How far the count has come, 0 to 1.
    required property real fraction
    /// Whether something is still being counted: the bar and the button's word are for that.
    required property bool counting

    objectName: "import-status"
    implicitHeight: lines.height + 2 * Widths.space.row
    height: implicitHeight
    radius: Theme.cardRadius
    color: Theme.surface

    Accessible.role: Accessible.StaticText
    Accessible.name: status.words

    Column {
        id: lines

        x: Widths.space.row
        y: Widths.space.row
        width: parent.width - 2 * Widths.space.row
        spacing: Widths.scale.room(8)

        Item {
            width: parent.width
            height: Math.max(sentence.height, waits.height)

            Text {
                id: sentence

                objectName: "import-status-words"
                anchors.left: parent.left
                anchors.right: waits.visible ? waits.left : parent.right
                anchors.rightMargin: waits.visible ? Widths.space.row : 0
                text: status.words
                textFormat: Text.PlainText
                color: Theme.ink
                elide: Text.ElideRight
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.body(13)
                font.weight: Font.DemiBold
            }

            // Said only where it fits on the sentence's own line: the greyed button carries
            // the same words in its tooltip, so a narrow column loses nothing by dropping it.
            Text {
                id: waits

                objectName: "import-status-waits"
                anchors.right: parent.right
                anchors.verticalCenter: sentence.verticalCenter
                visible: status.counting
                         && sentenceWidth.advanceWidth + waitsWidth.advanceWidth
                            + 2 * Widths.space.row <= lines.width
                text: DropCaptions.startWaitsLabel
                textFormat: Text.PlainText
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
            }

            TextMetrics {
                id: sentenceWidth

                font: sentence.font
                text: sentence.text
            }

            TextMetrics {
                id: waitsWidth

                font: waits.font
                text: waits.text
            }
        }

        Rectangle {
            objectName: "import-status-bar"
            visible: status.counting
            width: parent.width
            height: Widths.scale.size(4)
            radius: height / 2
            color: Theme.rule

            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, status.fraction))
                height: parent.height
                radius: parent.radius
                color: Theme.emerald

                Behavior on width {
                    NumberAnimation { duration: Preferences.motion === Preferences.Instant ? 0 : 140 }
                }
            }
        }
    }
}
