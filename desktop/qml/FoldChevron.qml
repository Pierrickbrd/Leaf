// The chevron of a line that folds: pointing right when shut, down when open.
//
// Drawn, not a glyph: it expresses a control's state — open, closed — never a thing, so it
// follows `LeafCheck`'s rule and not `LevelMark`'s. Its own file since the checking frame's
// tree folds as the import card's tree does: the second copy of these forty lines would have
// been the first to drift.

import QtQuick
import Leaf

Canvas {
    id: chevron

    property bool open: false
    /// Ink rather than soft ink, for a line under the pointer.
    property bool warm: false
    /// The colour, when the line it sits on is lit in a colour of its own.
    property color tint: warm ? Theme.ink : Theme.inkSoft

    width: Widths.scale.icon(10)
    height: width
    rotation: open ? 90 : 0
    transformOrigin: Item.Center

    Behavior on rotation {
        NumberAnimation { duration: 90 }
    }

    onWidthChanged: requestPaint()
    onTintChanged: requestPaint()
    onPaint: {
        const ink = getContext("2d")
        ink.reset()
        // Drawn on a grid of ten, whatever the side the window gives it.
        ink.scale(width / 10, height / 10)
        ink.strokeStyle = chevron.tint
        ink.lineWidth = 1.5
        ink.lineCap = "round"
        ink.beginPath()
        ink.moveTo(3, 1)
        ink.lineTo(7, 5)
        ink.lineTo(3, 9)
        ink.stroke()
    }
}
