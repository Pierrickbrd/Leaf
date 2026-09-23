// A handful of series tiles drawn from a plain list of maps.
//
// The shelf's own tile, in the size this block gives it: a series is drawn the same way
// wherever it appears, so the cover, the mark of one in progress and the grey line under the
// name all come from one file rather than from a second, smaller lookalike.
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

    readonly property real coverSide: 96

    spacing: Widths.shelfGap

    Repeater {
        model: shelf.tiles

        SeriesTile {
            required property var modelData

            seriesId: modelData.seriesId
            name: modelData.name
            work: modelData.name
            cover: modelData.cover
            medium: ""
            // « ici » is appended to the line the tile already has, rather than drawn over
            // the cover where it would have to be placed again at every size.
            volumes: modelData.here ? SeriesCaptions.hereToo(modelData.detail)
                                    : modelData.detail
            inProgress: modelData.inProgress
            howFarRead: modelData.howFarRead
            selected: modelData.here
            cellWidth: shelf.coverSide
            cellHeight: shelf.coverSide * 1.5 + 6 + 40 + 2 + 18
            coverWidth: shelf.coverSide
            coverHeight: shelf.coverSide * 1.5
            viewportClearance: 0
            onOpened: shelf.opened(modelData.seriesId)
        }
    }
}
