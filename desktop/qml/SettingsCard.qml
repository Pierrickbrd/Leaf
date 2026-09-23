// A group of settings, on the card every other surface of Leaf is drawn on.
//
// The first draft laid plain rows on bare paper and read as a debug print. The shelf, the
// band and the search all put what belongs together on a `surface` card with the same
// radius; a screen that does not is a screen that looks like it belongs to another
// application.

import QtQuick
import Leaf

Item {
    id: card

    /// The heading above the card, outside it. Inside, it would compete with the first row.
    required property string title
    default property alias content: body.data

    readonly property int padding: 16
    /// The heading is drawn outside the card and counted inside this item, which is what it
    /// was missing: with no height of its own it took none, and « APPARENCE » came to rest
    /// against the rule under the tabs. Whatever stacks these now leaves room for the words
    /// as well as the box.
    readonly property int titleHeight: heading.implicitHeight + 8

    width: parent ? parent.width : 0
    height: titleHeight + surface.height

    Text {
        id: heading

        objectName: "card-" + card.title
        anchors.left: parent.left
        anchors.top: parent.top
        text: card.title
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: 12
        font.weight: Font.Medium
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
    }

    Rectangle {
        id: surface

        anchors.left: parent.left
        anchors.right: parent.right
        y: card.titleHeight
        height: body.height + 2 * card.padding

        CardLift { level: CardLift.Card }
        radius: Theme.cardRadius
        color: Theme.surface
        // Lifted off the paper rather than outlined on it: an outline round every card makes
        // a form, and a settings screen of six outlined boxes reads as a form to fill in.
        border.color: "transparent"
        border.width: 1
        antialiasing: true

        Column {
            id: body

            x: card.padding
            y: card.padding
            width: surface.width - 2 * card.padding
            spacing: 10
        }
    }
}
