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
    spacing: 12

    /// As many as fit, at the size the width gives them — the rule the volumes grid and the
    /// shelf both follow. Held at three quarters of a shelf column, because these are read
    /// one after another and a wall of them would be a second shelf.
    readonly property real coverSide: {
        const columns = Math.max(1, Math.round(Widths.shelfColumns * 1.35))
        return Math.max(64, (width + spacing) / columns - spacing)
    }

    Repeater {
        model: shelf.tiles

        MiniTile {
            required property var modelData

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
