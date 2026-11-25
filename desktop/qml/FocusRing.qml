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
    /// How far past the item the ring's gap begins: nothing for a ring against its control,
    /// more for one that has to clear a mark of its own drawn round the item — the profile
    /// reading now, on « Qui lit ? ». The ring stays concentric with the item either way.
    property int standOff: 0

    /// A ring drawn inside the item, on its own edge, for a control that stands in a list that
    /// clips it and whose neighbours touch it: round it, the ring was cut off by the list on
    /// three sides and covered by the next row on the fourth. The control is tinted as it is
    /// under the pointer, so the ring is its own edge and not a frame round something wider.
    property bool inside: false

    anchors.fill: parent
    anchors.margins: inside ? 0 : -(standOff + Theme.focusGap + thickness)
    radius: inside ? cornerRadius : cornerRadius + standOff + Theme.focusGap + thickness

    color: inside ? Theme.onPaper : "transparent"
    border.color: Theme.emerald
    border.width: thickness
    /// What the ring follows. A control whose focus is not its own `activeFocus` names it here
    /// rather than overriding `visible`, so the rule below still holds for it.
    property bool focused: parent ? parent.activeFocus : false
    readonly property var cursor: Window.window ? Window.window.navigationCursor : null

    // Hidden, not moved, once the pointer has emphasised a cover: the focus stays where the
    // keyboard left it so the next key continues from there, but two things lit at once — a
    // chip ringed and a cover hovered — left a reader unsure which one Enter would open. And
    // still hidden after the pointer leaves, until a key brings it back: reappearing on its
    // own, it lit a chip nobody had gone back to.
    visible: focused && !(cursor && cursor.pointerTookOver)
}
