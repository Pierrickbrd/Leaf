// One shared nine-slice shadow, never a blur recomputed by every scrolling delegate.
//
// A cover's, and the resume band's card: the same measured elevation, and the nine-slice's
// stretching middle is what lets one texture serve a 2:3 cover and a band as wide as a window.
//
// The transparent source is 80 × 104. Its 48 × 72 centre has the cover's 2:3 shape; the
// borders below keep its rounded corners and blurred edges fixed while stretching only the
// quiet middle. Sixteen pixels on either side, twelve above and twenty below are left for
// the two elevations measured in the artifact.

import QtQuick
import Leaf

BorderImage {
    id: shadow

    /// What the shadow is cast under, for the objectName alone — a series identifier under a
    /// cover, "resume" under the band's card.
    required property string under

    /// How far it bleeds, in proportion to what casts it. The elevation was measured on the
    /// header's cover, a hundred and thirty-eight pixels wide; the same halo under a cover a
    /// fifth of that is a smudge with a stamp in the middle. One rule rather than a number
    /// per caller — five numbers is five things to get wrong, which is what the lift was.
    readonly property real spread: Math.max(0.35, Math.min(1, width / 138))

    objectName: "cover-shadow-" + under
    anchors.fill: parent
    anchors.leftMargin: -16 * shadow.spread
    anchors.rightMargin: -16 * shadow.spread
    anchors.topMargin: -12 * shadow.spread
    anchors.bottomMargin: -20 * shadow.spread

    source: Theme.dark
            ? Qt.resolvedUrl("assets/cover-shadow-dark.png")
            : Qt.resolvedUrl("assets/cover-shadow-light.png")
    asynchronous: false
    cache: true
    smooth: true

    border.left: 32
    border.right: 32
    border.top: 28
    border.bottom: 36
    horizontalTileMode: BorderImage.Stretch
    verticalTileMode: BorderImage.Stretch
}
