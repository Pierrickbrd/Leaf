// One shared nine-slice shadow, never a blur recomputed by every scrolling delegate.
//
// The transparent source is 80 × 104. Its 48 × 72 centre has the cover's 2:3 shape; the
// borders below keep its rounded corners and blurred edges fixed while stretching only the
// quiet middle. Sixteen pixels on either side, twelve above and twenty below are left for
// the two elevations measured in the artifact.

import QtQuick
import Leaf

BorderImage {
    id: shadow

    required property string seriesId

    objectName: "cover-shadow-" + seriesId
    anchors.fill: parent
    anchors.leftMargin: -16
    anchors.rightMargin: -16
    anchors.topMargin: -12
    anchors.bottomMargin: -20

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
