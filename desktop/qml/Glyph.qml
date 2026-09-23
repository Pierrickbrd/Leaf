// A Material symbol, tinted.
//
// The SVGs ship without a fill, so a glyph is never one item: it is an `Image` drawn hidden
// and a `ColorOverlay` anchored to it that paints it. Written out, that is six lines and a
// `visible: false` nobody may forget — forget it and the untinted glyph shows through under
// the tinted one, which on a dark palette reads as a smudge rather than as a mistake.
//
// Eleven places wrote those six lines before this file existed, in ten files, at five
// different sizes. They are the same six lines: what changes is which symbol, how big, and
// what colour.
//
// It sizes itself, so a caller places *this* — `anchors.centerIn`, `x: 12`, whatever the
// design asks — rather than placing an image and then chasing it with an overlay.

import QtQuick
import Qt5Compat.GraphicalEffects
import Leaf

Item {
    id: glyph

    /// The whole path, because some callers compose it from a name and others choose between
    /// two symbols on a condition. Both are the same thing to this.
    property alias source: mark.source
    property int side: 20
    property color tint: Theme.inkSoft
    /// What the SVG is rasterised at, when that is not the size it is drawn at. `LevelMark`
    /// asks for twice its own: it is the one glyph here that is scaled rather than placed.
    property int resolution: glyph.side

    implicitWidth: glyph.side
    implicitHeight: glyph.side
    width: implicitWidth
    height: implicitHeight

    Image {
        id: mark

        anchors.fill: parent
        // Asked for at the size it is drawn: an SVG rasterised at its own size and scaled
        // down is an SVG drawn soft, and these are drawn small.
        sourceSize: Qt.size(glyph.resolution, glyph.resolution)
        fillMode: Image.PreserveAspectFit
        smooth: true
        visible: false
    }

    ColorOverlay {
        anchors.fill: mark
        source: mark
        color: glyph.tint
    }
}
