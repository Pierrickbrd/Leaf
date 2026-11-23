// A row of controls under the bar — tabs, the filter button and its pills — on paper of its
// own, at the page's edge.
//
// Three rows did this three ways on 23/10/2026: the search's tabs at `y: 9` in a band of 52,
// the settings' centred in 56 with a rule, the filters at twice the shelf's gap round a
// button of 34. The owner saw the first of them stand higher than the « Reprendre » card on
// the shelf next to it. A strip is now as tall as what it holds, `top` above it when it is
// the first thing on the page, `block` below it, and its controls start at the gutter. No
// strip has a rule under it: the settings' alone had one, and two rows of the same tabs read
// as two grammars.
//
// What it holds goes in one row, `block` apart: the filter button and the pills it produces. The pills among themselves are the holder's own
// row, `pill` apart. Paper and not glass: the shelf's band slides out under the filters'
// strip, and a strip that let it show through would read as two rows at once.

import QtQuick
import Leaf

Item {
    id: strip

    /// `top` when the strip is the first thing under the bar; `block` under a block, or none
    /// under another strip, whose `bottomPadding` already parted them.
    property int topPadding: Widths.space.top
    property int bottomPadding: Widths.space.block
    /// The row the controls stand in, for whoever measures where they start.
    readonly property Item line: line

    default property alias content: line.data

    implicitHeight: topPadding + line.height + bottomPadding

    Rectangle {
        anchors.fill: parent
        color: Theme.paper
    }

    Row {
        id: line

        objectName: strip.objectName.length > 0 ? strip.objectName + "-line" : ""
        x: Widths.space.gutter
        y: strip.topPadding
        spacing: Widths.space.block
    }
}
