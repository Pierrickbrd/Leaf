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

    spacing: 10
    visible: tiles.length > 0

    BlockHeading {
        width: block.width
        title: block.title
        line: block.line
    }

    Column {
        id: between

        width: block.width
        spacing: 6
    }

    TileShelf {
        width: block.width
        tiles: block.tiles
        onOpened: seriesId => block.opened(seriesId)
    }
}
