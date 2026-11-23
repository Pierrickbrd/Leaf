// The dashed rounded edge of a word nobody typed where it is shown: `DashedChip`'s « Calculé »,
// and the credit a volume draws from its series in its window. Qt Quick draws no dashed border,
// so the edge is painted; once here, so that the two do not drift apart in their dashes.

import QtQuick
import Leaf

Canvas {
    id: edge

    property color ink: Theme.emerald
    property color fill: Theme.surface
    /// A pill by default; a box's own corner when it edges a box.
    property real radius: (height - 1) / 2

    onInkChanged: requestPaint()
    onFillChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onRadiusChanged: requestPaint()

    onPaint: {
        const context = getContext("2d")
        context.reset()
        // Never below 0: a box not laid out yet is 0 high, and `arcTo` refuses a negative radius.
        const r = Math.max(0, Math.min(edge.radius, (height - 1) / 2))
        context.beginPath()
        context.moveTo(0.5 + r, 0.5)
        context.arcTo(width - 0.5, 0.5, width - 0.5, height - 0.5, r)
        context.arcTo(width - 0.5, height - 0.5, 0.5, height - 0.5, r)
        context.arcTo(0.5, height - 0.5, 0.5, 0.5, r)
        context.arcTo(0.5, 0.5, width - 0.5, 0.5, r)
        context.closePath()
        context.fillStyle = edge.fill
        context.fill()
        context.setLineDash([3, 2])
        context.lineWidth = 1
        context.strokeStyle = edge.ink
        context.stroke()
    }
}
