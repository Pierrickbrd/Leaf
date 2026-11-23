// A surface that holds a group of things: the settings' cards, and every card after them.
//
// Six paddings for « the inside of a card » were measured on 23/10/2026 — 14, 16, 17, 18, 20
// and 22 — and three gaps between its lines. This is the one: `Widths.space.card` round what
// it holds, `Widths.space.row` between its lines, on the lifted surface every other card of
// Leaf is drawn on. A card that has a reason to differ says so in its own file.

import QtQuick
import Leaf

Rectangle {
    id: card

    default property alias content: body.data
    readonly property int padding: Widths.space.card

    implicitHeight: body.height + 2 * padding

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
        width: card.width - 2 * card.padding
        spacing: Widths.space.row
    }
}
