// The shadow a card is laid on the paper with.
//
// Written out in five places before this file existed, each with its own pair of numbers and
// each carrying the same comment saying it was the same lift. It is: a rectangle the shape of
// what it sits under, pushed down and darkened in proportion to how far off the page that
// thing is.
//
// Four heights and no more, because this client lays things at four — a card on the page, a
// bubble over it, a menu over that, a modal over everything — and a fifth would be a value
// somebody picked rather than a decision anybody made. That is what the five copies were
// drifting towards: two of them already disagreed by a pixel for no reason anyone could name.
//
// Placed inside the thing it lifts, which must carry a `radius`; it takes its shape from it.

import QtQuick
import Leaf

Rectangle {
    id: lift

    enum Height { Card, Bubble, Menu, Modal }

    property int level: CardLift.Card

    /// How far the shadow falls, and how heavy it is. A thing further off the paper throws a
    /// longer and darker one — which is the whole of what tells a modal from a card when both
    /// are the same colour.
    readonly property int fall: [1, 3, 4, 6][lift.level]
    readonly property real weight: (Theme.dark ? [0.40, 0.45, 0.50, 0.60]
                                               : [0.12, 0.14, 0.16, 0.20])[lift.level]

    anchors.fill: parent
    anchors.topMargin: lift.fall
    radius: parent.radius
    color: "#000000"
    opacity: lift.weight
    // Under what it lifts, never over it.
    z: -1
}
