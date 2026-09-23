// A cover cut to its rounding, and not merely clipped to the box around it.
//
// `clip: true` on a `Rectangle` clips to the rectangle, **never to its `radius`** — so an
// image laid inside one keeps its four square corners and paints them over the rounding.
// `CoverSkin` then draws its hairline round the shape the rectangle *claims*, and what one
// sees is a rounded border with the picture poking out at each corner. Invisible on a cover
// whose own edges are dark, plain on every pale one, which is why it stood for a fortnight
// on the header of every series page.
//
// Qt 6.4 has no `MultiEffect`, so the mask is the `ShaderEffectSource` + `OpacityMask` pair
// the shelf tile and the volume tile had each written out for themselves. One file, for the
// reason `CoverSkin` gives: five covers needing the same six lines is five places to get one
// of them wrong, and three of the five already were.

import QtQuick
import Qt5Compat.GraphicalEffects
import Leaf

Item {
    id: cover

    property alias source: picture.source
    /// The image's own name, for a test that looks it up. The rounding is on this item, so
    /// the picture inside it cannot carry the name the caller wants to find.
    property alias pictureName: picture.objectName
    property int radius: Theme.coverRadius

    /// Whatever else belongs *under* the rounding — a progress bar along the bottom edge, the
    /// shade that keeps it off a green cover. Declared children land here, so they are masked
    /// with the picture rather than laid over it with square corners of their own.
    default property alias inside: masked.data

    // What shows while the picture is still coming, and behind one that does not fill.
    Rectangle {
        anchors.fill: parent
        radius: cover.radius
        color: Theme.onPaper
        antialiasing: true
    }

    Item {
        id: masked

        anchors.fill: parent

        Image {
            id: picture

            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
        }
    }

    ShaderEffectSource {
        id: texture

        sourceItem: masked
        hideSource: true
        visible: false
    }

    OpacityMask {
        anchors.fill: parent
        source: texture
        maskSource: Rectangle {
            width: cover.width
            height: cover.height
            radius: cover.radius
            antialiasing: true
        }
    }
}
