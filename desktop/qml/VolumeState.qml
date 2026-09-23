// What a line says about where the reader stands: one object, at the same place on every row.
//
// A ring and not the bar a cover carries. A cover has a bottom edge to lay a bar along; a line
// of a list has none, which is the whole of the difference — the same fact, drawn with what
// each shape offers. The exact fraction goes with it: « 12/54 » becomes « not far in », and
// that is a good exchange, because the precise page is in the resume band just above.
//
// `Canvas` and not `QtQuick.Shapes`, which is a module this build does not carry — the import
// dialog settled that already.

import QtQuick
import Leaf

Item {
    id: state

    /// `Entries.State`, as the model hands it over.
    required property int reading
    required property real howFarRead

    readonly property bool finished: reading === 2
    readonly property bool started: reading === 1

    objectName: "volume-state"
    implicitWidth: 24
    implicitHeight: 24

    Canvas {
        id: ring

        anchors.fill: parent
        visible: state.finished || state.started
        antialiasing: true

        // Redrawn when either changes: a canvas paints once and would otherwise keep the arc
        // of the row this delegate was recycled from.
        Connections {
            target: state
            function onReadingChanged() { ring.requestPaint() }
            function onHowFarReadChanged() { ring.requestPaint() }
        }

        onPaint: {
            const context = getContext("2d")
            context.reset()
            const middle = width / 2
            const radius = middle - 1.5
            // A little thinner than a button's outline: it is a mark on a line, not a control.
            context.lineWidth = 2

            context.beginPath()
            context.arc(middle, middle, radius, 0, 2 * Math.PI)
            context.strokeStyle = Theme.rule
            context.stroke()

            const part = state.finished ? 1 : Math.max(0.04, state.howFarRead)
            context.beginPath()
            context.arc(middle, middle, radius, -Math.PI / 2,
                        -Math.PI / 2 + part * 2 * Math.PI)
            context.strokeStyle = Theme.emerald
            context.stroke()
        }
    }

    // Filled when it is done, so the ring and its centre say the same thing twice over —
    // a colour never carries a meaning on its own here.
    Rectangle {
        anchors.centerIn: parent
        width: 16
        height: 16
        radius: 99
        visible: state.finished
        color: Theme.emerald
        antialiasing: true
    }

    // The same two strokes `LeafCheck` draws, for the same reason: a check from a font is a
    // different glyph on every machine.
    Rectangle {
        x: 6.5
        y: 13
        width: 5.5
        height: 2
        radius: 1
        rotation: 45
        visible: state.finished
        color: Theme.onEmerald
        antialiasing: true
    }

    Rectangle {
        x: 9
        y: 11.8
        width: 8.5
        height: 2
        radius: 1
        rotation: -45
        visible: state.finished
        color: Theme.onEmerald
        antialiasing: true
    }

    // Under way: a triangle rather than a fraction, for the reason above.
    Canvas {
        anchors.fill: parent
        visible: state.started
        antialiasing: true

        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.beginPath()
            context.moveTo(width / 2 - 1.8, height / 2 - 3)
            context.lineTo(width / 2 + 3.2, height / 2)
            context.lineTo(width / 2 - 1.8, height / 2 + 3)
            context.closePath()
            context.fillStyle = Theme.emerald
            context.fill()
        }
    }
}
