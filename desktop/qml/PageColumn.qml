// Where a page's content stands: `gutter` from the window's sides, `top` under what is above.
//
// The one place that knows where a page begins. On 23/10/2026 every screen placed its own
// first block — the band by its inset, the filters by twice the shelf's gap, the search tabs
// at `y: 9`, the back button at `y: 12`, the settings' tabs centred in 56 — and the owner,
// setting the shelf beside the search, saw « Reprendre » and « Aperçu – Séries – Fichiers »
// stand at different heights under the same bar. A page in one of these starts at the same
// place as every other.
//
// `bounded` is for pages of reading — the files found, later the series and the settings —
// which stop at `Widths.space.column` on the left rather than running to 1888 pixels with
// their words at one end and their actions at the other. A grid of covers is never bounded:
// its columns fill the width, and its cells keep the gutter themselves (`viewportClearance`),
// which is the same rule applied by the cell so its focus ring has room.
//
// Content goes in `frame`, whose width is the column's; it stacks itself (a `Column`) or
// fills the frame. Given no height, the page is as tall as what it holds plus its paddings;
// given one, the frame fills it and what is in it can centre itself there.

import QtQuick
import Leaf

Item {
    id: page

    /// Held to `Widths.space.column`, on the left.
    property bool bounded: false
    /// Under the bar, `top`; under a strip that already left its `block`, none.
    property int topPadding: Widths.space.top
    property int bottomPadding: Widths.space.top
    /// The width the content is laid in, after the gutters and the bound.
    readonly property real measure: frame.width
    readonly property Item frame: frame

    default property alias content: frame.data

    implicitWidth: parent ? parent.width : 0
    implicitHeight: topPadding + frame.childrenRect.y + frame.childrenRect.height
                    + bottomPadding

    Item {
        id: frame

        objectName: page.objectName.length > 0 ? page.objectName + "-frame" : ""
        x: Widths.space.gutter
        y: page.topPadding
        width: {
            const room = Math.max(0, page.width - 2 * Widths.space.gutter)
            return page.bounded ? Math.min(room, Widths.space.column) : room
        }
        height: Math.max(0, page.height - page.topPadding - page.bottomPadding)
    }
}
