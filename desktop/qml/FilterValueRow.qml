// One value of one axis: a mark, a name and its count, on a row you can click anywhere on.
//
// A square mark and not the round one `LeafMenuItem` draws: a sort order is one thing at a
// time and a radio says so, where several genres can be lit at once and a box says that. The
// shape is the rule, which is why it is not a colour.

import QtQuick
import Leaf

Rectangle {
    id: row

    required property string value
    required property string label
    required property bool lit
    /// Where the mark starts: the axis's title column, which the axis hands down.
    property int indent: Widths.scale.size(28)

    signal triggered()

    objectName: "filter-value-" + value
    height: Math.max(Widths.scale.size(30), name.implicitHeight + Widths.space.row)
    radius: 8
    color: pointer.hovered ? Theme.onPaper : "transparent"

    Accessible.role: Accessible.CheckBox
    Accessible.name: row.label
    Accessible.checkable: true
    Accessible.checked: row.lit
    Accessible.onPressAction: row.triggered()

    /// A stop of the panel's walk — see `KeyWalk`.
    readonly property bool keyStop: true

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            row.triggered()
            event.accepted = true
        }
    }

    HoverHandler {
        id: pointer

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: row.triggered()
    }

    Rectangle {
        id: mark

        objectName: "filter-mark-" + row.value
        x: row.indent
        anchors.verticalCenter: parent.verticalCenter
        width: Widths.scale.icon(16)
        height: width
        radius: 4
        color: row.lit ? Theme.emerald : "transparent"
        border.color: row.lit ? Theme.emerald : Theme.rule
        border.width: row.lit ? 0 : 1
        antialiasing: true

        // Round the box alone, as `LeafCheck`: the row is as wide as the list, and a ring round
        // it was cut off by the list on three sides and covered by the next row on the fourth.
        FocusRing {
            objectName: row.objectName + "-focus"
            cornerRadius: mark.radius
            focused: row.activeFocus
        }

        // Drawn rather than typed: a check mark from a font is a different glyph on every
        // machine, and two of the three are not centred in their own box. Drawn on the box's
        // grid of 16 and scaled with it, so the tick stays where it was drawn.
        Item {
            width: 16
            height: 16
            anchors.centerIn: parent
            scale: mark.width / 16

            Rectangle {
                x: 3
                y: 8
                width: 5
                height: 2
                radius: 1
                rotation: 45
                visible: row.lit
                color: Theme.onEmerald
                antialiasing: true
            }

            Rectangle {
                x: 5.5
                y: 7
                width: 8
                height: 2
                radius: 1
                rotation: -45
                visible: row.lit
                color: Theme.onEmerald
                antialiasing: true
            }
        }
    }

    Text {
        id: name

        objectName: "filter-value-text-" + row.value
        anchors.left: mark.right
        anchors.leftMargin: Widths.space.row
        anchors.right: parent.right
        anchors.rightMargin: Widths.space.row
        anchors.verticalCenter: parent.verticalCenter
        textFormat: Text.PlainText
        text: row.label
        color: row.lit ? Theme.emerald : Theme.ink
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.body(14)
        font.weight: row.lit ? Font.Medium : Font.Normal
        elide: Text.ElideRight
    }
}
