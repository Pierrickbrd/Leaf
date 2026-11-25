// A rounded word that can be pressed, and how it answers the pointer.
//
// Two kinds share it: the filter chips over the shelf, which light and go out where they
// stand, and the genres of a series page, which take you to the shelf narrowed to theirs.
// They had two hovers — a chip took `rule` behind its word, a genre only darkened the word —
// and the same pill reading two ways under the same hand is a pill nobody learns. One look,
// written here: under the pointer the whole pill turns emerald, background and word.
//
// Lit is the other state, and it keeps what tells it apart from a hover: the emerald rim and
// the heavier word. A hover goes when the hand does; a rim says what is in force.

import QtQuick
import Leaf

Rectangle {
    id: pill

    required property string label
    /// What is drawn as the word when the pill carries a count of its own: `label` is then
    /// the whole, for whoever reads the screen aloud, and `word` the part drawn here.
    property string word: label
    /// A number drawn apart from the word, in a badge centred on the pill's height. Set on a
    /// line of text it sat on the word's baseline, and a figure is taller than the lowercase
    /// beside it: « Non lues 7 » read as a word with a digit dropped after it.
    property int tally: -1
    property bool lit: false
    /// The word's size, which the two kinds do not share: a chip is a command of the shelf,
    /// a genre is a line of a civil status.
    property int textSize: 14
    property int padding: 14

    signal pressed()

    readonly property bool hovered: pointer.hovered
    /// How tall the word stands, for a pill that takes its height from it.
    readonly property real wordHeight: said.implicitHeight
    readonly property bool warm: lit || hovered

    implicitWidth: row.implicitWidth + 2 * padding
    width: implicitWidth
    radius: height / 2
    color: warm ? Theme.emeraldWash : Theme.onPaper
    border.color: lit ? Theme.emerald : "transparent"
    border.width: 1
    antialiasing: true

    HoverHandler {
        id: pointer

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: pill.pressed()
    }

    Row {
        id: row

        anchors.centerIn: parent
        spacing: Widths.scale.fit(7)

        Text {
            id: said

            objectName: pill.objectName + "-text"
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: pill.word
            color: pill.warm ? Theme.emerald : Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: pill.textSize
            font.weight: pill.lit ? Font.Medium : Font.Normal
        }

        // In the word's own colour at a fraction of its strength, so that it is right in every
        // state — resting, hovered, lit — without a colour of its own for each.
        Rectangle {
            id: badge

            objectName: pill.objectName + "-tally"
            visible: pill.tally >= 0
            anchors.verticalCenter: parent.verticalCenter
            height: Math.round(figure.implicitHeight * 0.95)
            width: Math.max(height, figure.implicitWidth + Widths.scale.fit(10))
            radius: height / 2
            color: Qt.alpha(figure.color, 0.16)
            antialiasing: true

            Text {
                id: figure

                anchors.centerIn: parent
                textFormat: Text.PlainText
                text: pill.tally
                color: pill.warm ? Theme.emerald : Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Math.round(pill.textSize * 0.86)
                font.weight: Font.Medium
            }
        }
    }
}
