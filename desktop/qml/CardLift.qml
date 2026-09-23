// The shadow a card is laid on the paper with.
//
// Written out in five places before this file existed, each with its own pair of numbers and
// each carrying the same comment saying it was the same lift. It is: a shadow the shape of
// what it sits under, pushed down, softened and darkened in proportion to how far off the
// page that thing is.
//
// Four heights and no more, because this client lays things at four — a card on the page, a
// bubble over it, a menu over that, a modal over everything — and a fifth would be a value
// somebody picked rather than a decision anybody made. That is what the five copies were
// drifting towards: two of them already disagreed by a pixel for no reason anyone could name.
//
// Blurred, and no longer a darker rectangle laid under the first. A hard one had an edge of
// its own, and where it fell across a pale cover it drew a dark band along the bottom of the
// filter panel rather than a shadow. `RectangularGlow` and not `MultiEffect`, which Qt 6.4 —
// the one the CI builds with — does not have: the covers already lean on Qt5Compat for their
// mask, for the same reason.
//
// Placed inside the thing it lifts, which must carry a `radius`; it takes its shape from it.

import QtQuick
import Qt5Compat.GraphicalEffects
import Leaf

RectangularGlow {
    id: lift

    enum Height { Card, Bubble, Menu, Modal }

    property int level: CardLift.Card

    /// How far the shadow falls, how wide it spreads and how heavy it is. A thing further off
    /// the paper throws a longer, softer and darker one — which is the whole of what tells a
    /// modal from a card when both are the same colour.
    readonly property int fall: [1, 3, 4, 8][lift.level]
    readonly property int blur: [4, 8, 12, 20][lift.level]
    readonly property real weight: (Theme.dark ? [0.40, 0.45, 0.55, 0.65]
                                               : [0.10, 0.12, 0.16, 0.22])[lift.level]

    anchors.fill: parent
    anchors.topMargin: lift.fall
    anchors.bottomMargin: -lift.fall
    glowRadius: lift.blur
    spread: 0
    // The glow's own corner is measured at the outside of its blur: the shape's radius alone
    // would give a shadow squarer than the card that throws it.
    cornerRadius: parent.radius + lift.blur
    color: Qt.rgba(0, 0, 0, lift.weight)
    // Under what it lifts, never over it.
    z: -1
}
