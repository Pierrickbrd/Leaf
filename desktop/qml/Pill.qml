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
    property bool lit: false
    /// The word's size, which the two kinds do not share: a chip is a command of the shelf,
    /// a genre is a line of a civil status.
    property int textSize: 14
    property int padding: 14

    signal pressed()

    readonly property bool hovered: pointer.hovered
    readonly property bool warm: lit || hovered

    implicitWidth: word.implicitWidth + 2 * padding
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

    Text {
        id: word

        objectName: pill.objectName + "-text"
        anchors.centerIn: parent
        text: pill.label
        color: pill.warm ? Theme.emerald : Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: pill.textSize
        font.weight: pill.lit ? Font.Medium : Font.Normal
    }
}
