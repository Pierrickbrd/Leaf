// On or off, drawn as a switch.
//
// Beside `LeafCheck` and not instead of it: a box says « choose this one », a switch says
// « this is on ». Four families of event, each either warning or not, are the second — and a
// column of checkboxes would have read as a list to pick from rather than as four things
// turned on.
//
// The travel is the whole of it: the knob moves, the track fills, and neither is a colour on
// its own. A switch that only changed colour would say nothing to a reader who cannot tell
// this emerald from this grey.
//
// Drawn as this client draws a chosen thing, and moved the way the iOS switch moves: the
// travel overshoots its stop and comes back, the knob stretches towards where it is going
// while it is held — the press is answered before the release decides anything — and the
// colours follow over the same stretch of time instead of jumping at the start of a travel
// that has not happened yet. It slid in 110 ms and stopped dead before.

import QtQuick
import Leaf

Item {
    id: lever

    /// What it turns on, for whoever reads the screen aloud. Never drawn — the line beside it
    /// carries the words, and a switch with its own label in a table would say it twice.
    required property string label
    property bool on: false

    signal toggled(bool wanted)

    objectName: "leaf-switch"
    // The size of the switches a desktop draws elsewhere: at 34 by 19 it was the smallest
    // thing on its card, under a knob a fingertip covered whole. Still inside the 54 of the
    // column it is centred in.
    implicitWidth: 42
    implicitHeight: 24
    width: implicitWidth
    height: implicitHeight
    activeFocusOnTab: true

    Accessible.role: Accessible.CheckBox
    Accessible.name: lever.label
    Accessible.checkable: true
    Accessible.checked: lever.on
    Accessible.onPressAction: lever.toggled(!lever.on)

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            lever.toggled(!lever.on)
            event.accepted = true
        }
    }

    HoverHandler {
        id: pointer

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: press

        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: lever.toggled(!lever.on)
    }

    /// The knob's travel and its stretch, and the track's colour a little longer behind
    /// them: the iOS switch's own 300 and 400.
    readonly property int travel: 300
    readonly property int fill: 400
    /// CSS's `ease`, which is what the stretch and the colours run on: only the travel
    /// overshoots.
    readonly property var ease: [0.25, 0.1, 0.25, 1, 1, 1]
    /// Between the knob and the track's edge, all round.
    readonly property int inset: 3

    Rectangle {
        id: track

        anchors.fill: parent
        radius: height / 2
        // A wash and a ring rather than a slab of emerald: it is how a chosen pill is drawn
        // everywhere else in this client, and what moves is the knob.
        // Off and held, one step darker: the iOS switch fills its track under the press, and
        // this is that gesture in this client's colours.
        color: lever.on ? Theme.emeraldWash : (press.pressed ? Theme.rule : Theme.onPaper)
        border.color: lever.on ? Theme.emerald
                               : (pointer.hovered ? Theme.inkFaint : Theme.rule)
        border.width: 1
        antialiasing: true

        Behavior on color {
            ColorAnimation {
                duration: lever.fill
                easing.type: Easing.BezierSpline
                easing.bezierCurve: lever.ease
            }
        }
        Behavior on border.color {
            ColorAnimation {
                duration: lever.fill
                easing.type: Easing.BezierSpline
                easing.bezierCurve: lever.ease
            }
        }
    }

    Rectangle {
        id: knob

        objectName: lever.objectName + "-knob"
        /// Where along the track, from 0 at the off end to 1 at the on end. This is what
        /// travels, and `x` follows from it with the width the knob has at that moment.
        ///
        /// It was `x` that travelled, with its stop computed from the width — and the width
        /// animates too, as the knob comes back from its stretch. Every frame of that moved
        /// the stop, every move restarted the travel from where it stood, and the knob sat
        /// still for 350 ms before setting off; meanwhile it was placed as if already short,
        /// and the stretched knob stood out past the end of the track.
        property real along: lever.on ? 1 : 0

        y: lever.inset
        // Pinned to the end it rests at, so a stretch grows towards the other one.
        x: lever.inset + along * (lever.width - 2 * lever.inset - width)
        height: lever.height - 2 * lever.inset
        // Held down, longer by two fifths of its height: what the iOS switch does.
        width: press.pressed ? height * 1.4 : height
        radius: height / 2
        color: lever.on ? Theme.emerald : Theme.inkFaint
        antialiasing: true

        Behavior on color {
            ColorAnimation {
                duration: lever.fill
                easing.type: Easing.BezierSpline
                easing.bezierCurve: lever.ease
            }
        }

        Behavior on along {
            NumberAnimation {
                duration: lever.travel
                // Out fast, past the stop and back, as the iOS switch does — by about a
                // pixel, inside the three between the knob and the edge. Its own curve went
                // past by a tenth of the travel and the knob was seen leaving the track.
                easing.type: Easing.OutBack
                easing.overshoot: 1.2
            }
        }
        // The stretch does not bounce: a knob that overshot its own length would wobble.
        Behavior on width {
            NumberAnimation {
                duration: lever.travel
                easing.type: Easing.BezierSpline
                easing.bezierCurve: lever.ease
            }
        }
    }

    FocusRing {
        objectName: lever.objectName + "-focus"
        cornerRadius: lever.height / 2
        focused: lever.activeFocus
    }
}
