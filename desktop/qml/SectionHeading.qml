// The name of a section, above what it names: « SÉRIES · 6 », « APPARENCE ».
//
// One style and one gap for a role that had three of each. On 23/10/2026 the settings put
// their content 8 pixels under the heading, the search 22 under « Séries » and 14 under
// « Fichiers », and the search's headings stood 4 pixels right of everything else on the
// page — four left edges on one screen. A heading starts where its column starts, at
// `x = 0`, and what it heads starts `Widths.space.heading` under it, which this counts in
// its own height: whatever stacks a heading over its content needs no gap of its own.
//
// `line` is what the section says about itself — « 3 éditions » over the tiles of the last
// tab — at the far end of the heading's width, which its holder sets to the width of what is
// really under it. Until the owner's pass of 23/10/2026 the last tab drew these with a
// heading of its own: capitals without the letter spacing, 10 pixels over its tiles where
// the settings left 8 — the same role in a second style.
//
// A `Text` and not an item holding one, so a heading is read and found as the words it is.
// The line is a child of it, and the words leave it room in their right padding: elided
// first, they never run under it, and `implicitWidth` is heading, gap and line together.

import QtQuick
import Leaf

Text {
    id: heading

    textFormat: Text.PlainText
    property string line: ""

    color: Theme.inkFaint
    font.family: Theme.textFamily
    font.pixelSize: Widths.scale.caption(12)
    font.weight: Font.Medium
    font.capitalization: Font.AllUppercase
    font.letterSpacing: Widths.scale.letters(0.8)
    bottomPadding: Widths.space.heading
    rightPadding: said.visible ? said.implicitWidth + Widths.scale.room(10) : 0
    elide: Text.ElideRight

    Text {
        id: said

        objectName: "block-count"
        anchors.right: parent.right
        anchors.baseline: parent.baseline
        width: Math.min(implicitWidth, parent.width)
        horizontalAlignment: Text.AlignRight
        textFormat: Text.PlainText
        text: heading.line
        visible: text.length > 0
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.body(13)
        elide: Text.ElideRight
    }
}
