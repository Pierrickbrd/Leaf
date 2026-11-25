// Eleven steps on a line, a word at either end: a webtoon's sensitivity and the speed of its A.
//
// Steps and not a free line, the choice of 24/11/2026: between ten and fifteen of them, and no
// value running to infinity — a reader comes back to the step they had, and says it in a
// number of notches. Dragged, clicked, or moved by the arrows once it holds the keys.

import QtQuick
import Leaf

Item {
    id: slider

    /// The step chosen, 0 to `last`.
    property int value: 0
    readonly property int last: 10
    /// What it chooses, for whoever reads the screen aloud.
    required property string label
    /// The words at either end, the slow or gentle one first.
    property var ends: ["", ""]
    readonly property real knob: Widths.scale.size(14)

    /// A step taken by the reader; the owner keeps it and hands it back as `value`.
    signal moved(int step)

    function stepAt(x) {
        const along = (x - slider.knob / 2) / Math.max(1, slider.width - slider.knob)
        return Math.max(0, Math.min(slider.last, Math.round(along * slider.last)))
    }

    function xOf(step) {
        return slider.knob / 2 + (slider.width - slider.knob) * step / slider.last
    }

    function take(step) {
        const kept = Math.max(0, Math.min(slider.last, step))
        if (kept !== slider.value)
            slider.moved(kept)
    }

    implicitHeight: slider.knob + Widths.scale.size(8) + ends.implicitHeight
    activeFocusOnTab: true
    Accessible.role: Accessible.Slider
    Accessible.name: slider.label
    Accessible.focusable: true

    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_Left:
        case Qt.Key_Down:
            slider.take(slider.value - 1)
            break
        case Qt.Key_Right:
        case Qt.Key_Up:
            slider.take(slider.value + 1)
            break
        case Qt.Key_Home:
            slider.take(0)
            break
        case Qt.Key_End:
            slider.take(slider.last)
            break
        default:
            return
        }
        event.accepted = true
    }

    Rectangle {
        id: track

        x: slider.knob / 2
        y: (slider.knob - height) / 2
        width: slider.width - slider.knob
        height: Widths.scale.size(4)
        radius: height / 2
        color: Theme.rule
    }

    Rectangle {
        objectName: slider.objectName + "-filled"
        x: track.x
        y: track.y
        width: slider.xOf(slider.value) - track.x
        height: track.height
        radius: track.radius
        color: Theme.emerald
    }

    Repeater {
        model: slider.last + 1

        Rectangle {
            required property int index

            x: Math.round(slider.xOf(index) - width / 2)
            y: track.y + track.height + Widths.scale.size(3)
            width: Math.max(1, Widths.scale.size(2))
            height: Widths.scale.size(5)
            color: index <= slider.value ? Theme.emerald : Theme.rule
        }
    }

    Rectangle {
        id: knob

        objectName: slider.objectName + "-knob"
        x: slider.xOf(slider.value) - width / 2
        width: slider.knob
        height: slider.knob
        radius: width / 2
        color: Theme.emerald
        border.color: Theme.surface
        border.width: Math.max(1, Widths.scale.size(2))

        FocusRing {
            objectName: slider.objectName + "-focus"
            cornerRadius: knob.radius
            focused: slider.activeFocus
        }
    }

    Item {
        id: ends

        y: slider.knob + Widths.scale.size(8)
        width: slider.width
        implicitHeight: from.implicitHeight

        Text {
            id: from

            textFormat: Text.PlainText
            text: slider.ends[0]
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(11)
        }

        Text {
            anchors.right: parent.right
            textFormat: Text.PlainText
            text: slider.ends[1]
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(11)
        }
    }

    MouseArea {
        anchors.fill: parent
        preventStealing: true
        cursorShape: Qt.PointingHandCursor
        onPressed: mouse => {
            slider.forceActiveFocus(Qt.MouseFocusReason)
            slider.take(slider.stepAt(mouse.x))
        }
        onPositionChanged: mouse => {
            if (pressed)
                slider.take(slider.stepAt(mouse.x))
        }
    }
}
