// The words under a series cover, measured once for every grid that lays tiles out.
//
// A grid sets its cell's height before a single tile exists, so it cannot ask a tile how
// tall its words came out: it used to add up `6 + 40 + 2 + 18`, the words as they were drawn
// at 1100. In the audit of 23/10/2026 that sum already cut « 12 albums » off the bottom of a
// two-line title at 800, and it could only get worse once words follow the desktop's text
// size. The shelf, the search's series and the shelf's skeleton read the same fonts and the
// same lines here, and the tile draws itself with them, so the cell and what is in it cannot
// disagree.

import QtQuick
import Leaf

QtObject {
    id: lines

    /// The series' name: a title, at most two lines, its line a quarter taller than its size.
    readonly property font titleFont: Qt.font({
        "family": Theme.displayFamily,
        "pixelSize": Widths.scale.title(16),
        "weight": Font.DemiBold
    })
    readonly property int titleLine: Math.round(titleFont.pixelSize * 1.25)
    readonly property int titleLines: 2

    /// « 12 albums »: a caption beside the cover.
    readonly property font countFont: Qt.font({
        "family": Theme.textFamily,
        "pixelSize": Widths.scale.caption(14)
    })
    readonly property int countLine: Math.ceil(countMetrics.height)

    /// The gap between the cover and the name, and between the name and the count.
    readonly property int above: Widths.scale.room(6)
    readonly property int between: Widths.scale.room(2)

    /// Everything under the cover, with a two-line name: what a cell holds below the cover.
    readonly property int under: above + titleLines * titleLine + between + countLine

    readonly property FontMetrics countMetrics: FontMetrics {
        font: lines.countFont
    }
}
