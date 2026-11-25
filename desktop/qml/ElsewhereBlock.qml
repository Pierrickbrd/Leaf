// One block of the last tab: a heading, what a way through needs when there is one, and the
// tiles under both.
//
// Three of them on that tab, and they are one file because they are one thing said three
// times — the editions of the work, the universe, and what a chosen way leaves out. The
// middle one has a chooser between its heading and its tiles, which is why anything written
// inside a block lands there rather than under the tiles.

import QtQuick
import Leaf

Column {
    id: block

    required property string title
    property string line: ""
    /// The maps `TileShelf` draws. An empty block is not drawn at all: a heading over nothing
    /// says less than no heading.
    required property var tiles

    /// What is written inside the block, between its heading and its tiles.
    default property alias inserted: between.data

    signal opened(string seriesId)

    visible: tiles.length > 0

    // The heading counts its own gap to what it heads, the settings' and the search's gap.
    // Its count ends over the last tile drawn and not at the block's far edge: held to the
    // block's width, it stood a thousand pixels from its heading over three tiles at 1100
    // (audit, 23/10/2026). Wider than the tiles, the heading keeps its own words whole.
    SectionHeading {
        objectName: block.objectName + "-heading"
        width: Math.min(block.width, Math.max(shelf.used, implicitWidth))
        text: block.title
        line: block.line
    }

    // A chooser between the heading and the tiles is one more line of the block: `row`
    // under it, as between the lines of a card.
    Column {
        width: block.width
        spacing: Widths.space.row

        Column {
            id: between

            width: block.width
            spacing: Widths.scale.room(6)
        }

        TileShelf {
            id: shelf

            width: block.width
            tiles: block.tiles
            onOpened: seriesId => block.opened(seriesId)
        }
    }
}
