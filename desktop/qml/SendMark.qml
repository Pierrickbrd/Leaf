// The round button at the end of a line of the third step: it sends the line, stops it, sends
// it again after a failure, and says it arrived — one place on the line, one shape, whatever
// the line is doing.
//
// The only circle it ever draws is how far the line has gone: an arc in the emerald, or in the
// alert when the line failed, and only once the line has begun. At rest — to send, to send
// again, paused at nothing — the button is its symbol and nothing round it: a track and an
// outline drawn at every state made a button of every line look already started, and the ring
// of one that really was could not be told from them. A finished line keeps its ticked circle,
// which is not a button. The arc is drawn rather than composed from rectangles because an arc
// is the one shape a rectangle cannot make, and `QtQuick.Shapes` is a module this build does
// not package (see the drop zone's own dashes).
//
// **It is always the same size and in the same place**, finished or not: the line puts it in
// a slot of its own, and a button that moved when the cross beside it appeared was one a
// reader chased down the list.

import QtQuick
import Leaf

Item {
    id: mark

    /// What it offers: `play`, `pause`, `retry`, or `done` — which is not a button.
    required property string kind
    /// How far the line has gone, 0 to 1.
    property real fraction: 0
    /// The emerald, or the alert for a line that failed.
    property color tone: Theme.emerald
    /// What it does, for a tooltip and for whoever reads the screen aloud.
    property string label: ""
    /// Drawn and not pressable: a line the server has not answered yet.
    property bool waiting: false

    readonly property bool pressable: mark.kind !== "done" && !mark.waiting
    readonly property bool hovered: pointer.hovered

    signal triggered()

    width: Widths.scale.size(40)
    height: width
    opacity: mark.waiting ? 0.5 : 1.0
    activeFocusOnTab: mark.pressable && mark.visible

    Accessible.role: mark.kind === "done" ? Accessible.StaticText : Accessible.Button
    Accessible.name: mark.label
    Accessible.focusable: mark.pressable
    Accessible.onPressAction: if (mark.pressable) mark.triggered()

    /// A stop of a screen that walks its stops with `KeyWalk`.
    readonly property bool keyStop: true

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            // Once for a key held down, as every button here.
            if (!event.isAutoRepeat && mark.pressable)
                mark.triggered()
            event.accepted = true
        }
    }

    onFractionChanged: ring.requestPaint()
    onToneChanged: ring.requestPaint()
    onKindChanged: ring.requestPaint()

    FocusRing {
        objectName: mark.objectName + "-focus"
        cornerRadius: mark.width / 2
    }

    Canvas {
        id: ring

        anchors.fill: parent
        antialiasing: true
        onPaint: {
            const ink = getContext("2d")
            ink.reset()
            const stroke = Math.max(2, Widths.scale.size(3))
            const centre = width / 2
            const radius = centre - stroke / 2 - 1
            const done = mark.kind === "done"
            if (done) {
                ink.beginPath()
                ink.arc(centre, centre, radius, 0, 2 * Math.PI)
                ink.fillStyle = Theme.emeraldWash
                ink.fill()
                ink.lineWidth = stroke
                ink.strokeStyle = Theme.emerald
                ink.stroke()
                return
            }
            if (mark.fraction > 0) {
                ink.beginPath()
                ink.lineWidth = stroke
                ink.lineCap = "round"
                ink.strokeStyle = mark.tone
                ink.arc(centre, centre, radius, -Math.PI / 2,
                        -Math.PI / 2 + 2 * Math.PI * Math.min(1, mark.fraction))
                ink.stroke()
            }
        }

        Connections {
            target: Theme

            function onChanged() {
                ring.requestPaint()
            }
        }
    }

    // Filled symbols of the rounded set, big enough to be read as the button's face with
    // nothing round it. Each is sized to weigh what the play does: the pause's two bars fill
    // more of their square than the triangle does, and the arrow of « relancer » is a drawn
    // outline that at 28 read as the largest thing on the line.
    Glyph {
        objectName: mark.objectName + "-glyph"
        anchors.centerIn: parent
        side: Widths.scale.icon(mark.kind === "done" ? 30
                                : mark.kind === "pause" ? 26
                                : mark.kind === "retry" ? 24 : 28)
        source: "assets/icons/" + (mark.kind === "play" ? "play_arrow_filled"
                                   : mark.kind === "pause" ? "pause"
                                   : mark.kind === "retry" ? "refresh" : "check") + ".svg"
        // The hover is the symbol lightening, since there is no outline left to light.
        tint: mark.kind === "done" ? Theme.emerald
              : mark.hovered && mark.pressable ? Qt.lighter(mark.tone, 1.3) : mark.tone
    }

    HoverHandler {
        id: pointer

        enabled: mark.pressable
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        enabled: mark.pressable
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: mark.triggered()
    }
}
