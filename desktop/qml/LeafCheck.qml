// A yes-or-no, drawn.
//
// The mark is drawn rather than typed, for the reason `FilterValueRow` gives about its own:
// a check from a font is a different glyph on every machine, and two of the three are not
// centred in their own box.
//
// Square, because a box is what several-at-once looks like and a circle is what one-of-many
// looks like. Neither of the two answers here is a choice among others.

import QtQuick
import Leaf

Item {
    id: box

    required property string label
    property bool checked: false
    /// A word under the label, for an answer whose cost is not obvious from its name.
    property string aside

    /// As wide as its box and its words and no wider, for one that has to stand in the middle
    /// of a page: at the width of its column the words start at the left of it, and the box
    /// and its label leave the centre.
    property bool snug: false
    /// Some of what it answers for, and not all: « Tout » over a list some lines of which are
    /// ticked. Drawn filled, with a bar where the tick goes.
    property bool partly: false
    /// The side of the box: smaller in a line of a list, where the box sits beside a name and
    /// not before a sentence.
    property real side: Widths.scale.icon(18)

    signal toggled(bool wanted)

    objectName: "leaf-check"
    implicitWidth: snug ? mark.width + Widths.space.label
                          + Math.ceil(Math.max(labelWidth.advanceWidth, asideWidth.advanceWidth))
                        : (parent ? parent.width : 0)
    // A box with no words is as tall as its mark: the height of an empty label, wrapped to
    // nothing, set the mark of a series nine pixels under the middle of its line.
    implicitHeight: box.label.length === 0 && box.aside.length === 0 ? mark.height : words.height
    width: implicitWidth
    height: implicitHeight
    activeFocusOnTab: false

    Accessible.role: Accessible.CheckBox
    Accessible.name: box.label
    Accessible.checkable: true
    Accessible.checked: box.checked
    Accessible.checkStateMixed: box.partly && !box.checked
    Accessible.focusable: true
    Accessible.focused: box.activeFocus
    Accessible.onPressAction: box.toggled(!box.checked)

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            // Once per press: held down, Space ticked and unticked a right, and asked the
            // server each time.
            if (!event.isAutoRepeat)
                box.toggled(!box.checked)
            event.accepted = true
        }
    }

    // What answers the hand: the box and its words, not the paper after them. The item is as
    // wide as its column so that a long label wraps, and the paper after a short one ticked
    // the box as if it were part of it.
    Item {
        width: mark.width + Widths.space.label + Math.max(label.contentWidth, aside.contentWidth)
        height: Math.max(box.height, mark.y + mark.height)

        HoverHandler {
            id: pointer

            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: box.toggled(!box.checked)
        }
    }

    Rectangle {
        id: mark

        objectName: box.objectName + "-mark"
        // Centred on the label's own line, not on the whole item: a box aligned to a block
        // two lines tall sits between them, next to nothing.
        y: box.label.length === 0 ? 0 : Math.round((label.height - height) / 2)
        width: box.side
        height: width
        radius: 5
        // **An empty box has to look like a box.** Transparent with a hairline in `rule` is
        // 1.2:1 against the card it sits on in the dark theme — invisible, and read as
        // « there is nothing to answer here ». Sunk and outlined in faint ink instead, so
        // the unanswered state is as legible as the answered one.
        readonly property bool filled: box.checked || box.partly
        color: mark.filled ? Theme.emerald : Theme.onPaper
        border.color: mark.filled ? Theme.emerald
                                  : (pointer.hovered ? Theme.ink : Theme.inkFaint)
        border.width: mark.filled ? 0 : 1
        antialiasing: true

        // Round the box, as round a switch's lever or a pill: the control is the box, and its
        // words say what it is. Round the whole line, the ring was a rectangle as wide as the
        // column with a word and empty paper inside it (owner, 25/10/2026).
        FocusRing {
            objectName: box.objectName + "-focus"
            cornerRadius: mark.radius
            focused: box.activeFocus
        }

        // Drawn on the box's own grid of 18 and scaled with it, so the tick stays where it
        // was drawn whatever side the window gives the box.
        Item {
            width: 18
            height: 18
            anchors.centerIn: parent
            scale: mark.width / 18

            Rectangle {
                x: 4
                y: 8
                width: 10
                height: 2
                radius: 1
                visible: box.partly && !box.checked
                color: Theme.onEmerald
                antialiasing: true
            }

            Rectangle {
                x: 3.5
                y: 9
                width: 6
                height: 2
                radius: 1
                rotation: 45
                visible: box.checked
                color: Theme.onEmerald
                antialiasing: true
            }

            Rectangle {
                x: 6.5
                y: 8
                width: 9
                height: 2
                radius: 1
                rotation: -45
                visible: box.checked
                color: Theme.onEmerald
                antialiasing: true
            }
        }
    }

    // The words as they run on one line, for `snug`: a `Text` wrapped to its column reports
    // the column.
    TextMetrics {
        id: labelWidth

        font: label.font
        text: box.label
    }

    TextMetrics {
        id: asideWidth

        font: aside.font
        text: box.aside
    }

    Column {
        id: words

        anchors.left: mark.right
        anchors.leftMargin: Widths.space.label
        anchors.right: parent.right
        spacing: Widths.space.tight

        Text {
            id: label

            objectName: box.objectName + "-label"
            width: parent.width
            textFormat: Text.PlainText
            text: box.label
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
            wrapMode: Text.Wrap
        }

        Text {
            id: aside

            objectName: box.objectName + "-aside"
            width: parent.width
            visible: text.length > 0
            textFormat: Text.PlainText
            text: box.aside
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            wrapMode: Text.Wrap
        }
    }
}
