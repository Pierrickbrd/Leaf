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
    default property alias content: surface.content

    /// The heading is drawn outside the card and counted inside this item, which is what it
    /// was missing: with no height of its own it took none, and « APPARENCE » came to rest
    /// against the rule under the tabs. Whatever stacks these now leaves room for the words
    /// as well as the box — the heading counts its own gap to the card.
    readonly property int titleHeight: heading.implicitHeight

    width: parent ? parent.width : 0
    height: titleHeight + surface.height

    SectionHeading {
        id: heading

        objectName: "card-" + card.title
        anchors.left: parent.left
        anchors.top: parent.top
        text: card.title
    }

    Card {
        id: surface

        anchors.left: parent.left
        anchors.right: parent.right
        y: card.titleHeight
        height: implicitHeight
    }
}
