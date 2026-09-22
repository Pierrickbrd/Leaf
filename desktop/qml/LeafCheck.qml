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

    signal toggled(bool wanted)

    objectName: "leaf-check"
    implicitWidth: parent ? parent.width : 0
    implicitHeight: words.height
    width: implicitWidth
    height: implicitHeight
    activeFocusOnTab: false

    Accessible.role: Accessible.CheckBox
    Accessible.name: box.label
    Accessible.checkable: true
    Accessible.checked: box.checked
    Accessible.onPressAction: box.toggled(!box.checked)

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            box.toggled(!box.checked)
            event.accepted = true
        }
    }

    HoverHandler {
        id: pointer

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: box.toggled(!box.checked)
    }

    Rectangle {
        id: mark

        objectName: box.objectName + "-mark"
        // Centred on the label's own line, not on the whole item: a box aligned to a block
        // two lines tall sits between them, next to nothing.
        y: Math.round((label.height - height) / 2)
        width: 18
        height: 18
        radius: 5
        // **An empty box has to look like a box.** Transparent with a hairline in `rule` is
        // 1.2:1 against the card it sits on in the dark theme — invisible, and read as
        // « there is nothing to answer here ». Sunk and outlined in faint ink instead, so
        // the unanswered state is as legible as the answered one.
        color: box.checked ? Theme.emerald : Theme.onPaper
        border.color: box.checked ? Theme.emerald
                                  : (pointer.hovered ? Theme.ink : Theme.inkFaint)
        border.width: box.checked ? 0 : 1
        antialiasing: true

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

    Column {
        id: words

        anchors.left: mark.right
        anchors.leftMargin: 10
        anchors.right: parent.right
        spacing: 1

        Text {
            id: label

            objectName: box.objectName + "-label"
            width: parent.width
            text: box.label
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }

        Text {
            objectName: box.objectName + "-aside"
            width: parent.width
            visible: text.length > 0
            text: box.aside
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
    }

    FocusRing {
        objectName: box.objectName + "-focus"
        cornerRadius: 6
    }
}
