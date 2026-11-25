// A handful of series tiles drawn from a plain list of maps.
//
// Its own tile and not the shelf's made smaller: these are read one after another to choose
// where to go next, where the shelf's are glanced at across a wall — and the design draws
// them differently for exactly that reason.
//
// A `Flow` and not a `GridView`: the page scrolls as one, and a view inside it would be a
// second scroll ending in white.

import QtQuick
import Leaf

Flow {
    id: shelf

    /// One map per tile: `seriesId`, `name`, `detail`, `cover`, `inProgress`, `howFarRead`
    /// and `here`. The same keys whether they come from the editions of a work or from the
    /// steps of a way through a universe.
    required property var tiles

    signal opened(string seriesId)


    // The design's own gap between these, which is tighter than the wall's: they are read
    // one after another rather than swept across.
    spacing: Widths.scale.room(12)

    /// The cover a tile is drawn at for a window of 1280: a length, so it grows with the
    /// window, and never narrower than its words — a card of words, so at a larger desktop
    /// text size the name under it keeps the width to wrap in.
    readonly property real drawnSide: Math.max(Widths.scale.size(108), Widths.scale.column(108))

    /// As many as fit at about that size, widened to share the row between them. It was a
    /// count read off the shelf's columns, whatever the width it was handed: a block bounded
    /// to a reading column would have shrunk its tiles rather than held fewer of them.
    readonly property int columns: Math.max(1, Math.floor((width + spacing)
                                                          / (drawnSide + spacing)))
    readonly property real coverSide: (width + spacing) / columns - spacing

    /// How much of the width the tiles actually take: one row of three tiles takes three,
    /// not the block's width. The heading above them is held to it.
    readonly property real used: {
        const across = Math.min(columns, tiles.length)
        return across > 0 ? across * (coverSide + spacing) - spacing : 0
    }

    // A count, and each tile reads its own entry: handed the list, every tile was rebuilt —
    // covers fetched again — whenever one series of the universe changed. See `FilterPills`.
    Repeater {
        model: shelf.tiles.length

        MiniTile {
            required property int index
            // Undefined for the moment between the list shrinking and the tile going.
            readonly property var modelData: shelf.tiles[index]
                                             ?? ({ "seriesId": "", "name": "", "detail": "",
                                                   "cover": "", "here": false })

            seriesId: modelData.seriesId
            name: modelData.name
            // « ici » is appended to the line the tile already has, rather than drawn over
            // the cover where it would have to be placed again at every size.
            detail: modelData.here ? SeriesCaptions.hereToo(modelData.detail)
                                   : modelData.detail
            cover: modelData.cover
            here: modelData.here
            side: shelf.coverSide
            onOpened: shelf.opened(modelData.seriesId)
        }
    }
}
