// The ring goes around the cover and never touches it.
//
// A cover is an arbitrary image, so a ring drawn on it depends on what it contains — green on
// a green cover disappears. The gap is what puts the ring on the paper instead, where the
// emerald has a contrast that is known: 5.12:1 light, 7.69:1 dark, whatever the illustration.
// Two layered tones would only make it a sticker.
//
// Qt Quick has no outline, so this is a rectangle drawn as a child of the cover item, expanded
// past its bounds by the gap and the thickness — not a rectangle behind it, and not clipped to
// it. The cover this is placed in must therefore not set `clip: true`, or the ring vanishes.

import QtQuick
import Leaf

Rectangle {
    id: ring

    property int thickness: 2
    property int cornerRadius: Theme.coverRadius

    anchors.fill: parent
    anchors.margins: -(Theme.focusGap + thickness)
    radius: cornerRadius + Theme.focusGap + thickness

    color: "transparent"
    border.color: Theme.emerald
    border.width: thickness
    visible: parent ? parent.activeFocus : false
}
