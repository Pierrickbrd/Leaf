// A profile, drawn: its colour, its initial, and a badge for what it is — a crown for the
// principal, a lock for a restricted profile. The same square in the bar (34 px) and on
// « Qui lit ? » (132 px), so the two can never disagree about who somebody is.

import QtQuick
import Leaf

Rectangle {
    id: square

    /// One entry of `Profiles.list`, or `Profiles.active`.
    property var profile: ({})
    property int side: 34

    readonly property string kind: profile && profile.kind ? profile.kind : ""
    /// The badge grows with the square but not in proportion — the mock-ups' two sizes, since
    /// one ratio cannot give a legible lock at 34 px without hiding the initial at 132.
    readonly property int badge: side >= 100 ? 36 : 18
    readonly property int overhang: side >= 100 ? 8 : 5

    width: side
    height: side
    radius: 10
    antialiasing: true
    color: Theme.profileTintIn(Theme.profileTints, profile && profile.color ? profile.color : "")

    Text {
        id: initial

        objectName: square.objectName + "-initial"
        // Centred on its ink and not on its line. Barlow keeps room for descenders under
        // every capital, so a letter centred by its line box sits visibly low in the square,
        // which the mock-ups showed at both sizes before this measured the glyph itself.
        TextMetrics {
            id: ink

            font: initial.font
            text: initial.text
        }

        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height / 2 - (ink.tightBoundingRect.y + ink.tightBoundingRect.height / 2)
           - baselineOffset
        text: square.profile && square.profile.initial ? square.profile.initial : ""
        color: Theme.profileInk
        font.family: Theme.displayFamily
        font.pixelSize: Math.round(square.side * (square.side >= 100 ? 0.5 : 0.62))
        font.weight: Font.Bold
    }

    Rectangle {
        objectName: square.objectName + "-badge"
        visible: square.kind === "principal" || square.kind === "restricted"
                 || square.kind === "other"
        x: square.width - square.badge + square.overhang
        y: square.height - square.badge + square.overhang
        width: square.badge
        height: square.badge
        radius: square.badge / 2
        color: Theme.surface
        border.color: Theme.rule
        border.width: 1
        antialiasing: true

        Glyph {
            anchors.centerIn: parent
            side: square.badge >= 36 ? 18 : 12
            // A kind this client was not taught is read as the narrowest, as `Profiles` does.
            source: square.kind === "principal" ? "assets/icons/crown.svg"
                                                : "assets/icons/lock.svg"
            tint: Theme.ink
        }
    }
}
