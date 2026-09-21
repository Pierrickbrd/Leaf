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

    signal triggered()

    objectName: "filter-value-" + value
    height: 30
    radius: 8
    color: pointer.hovered ? Theme.onPaper : "transparent"

    Accessible.role: Accessible.CheckBox
    Accessible.name: row.label
    Accessible.checkable: true
    Accessible.checked: row.lit
    Accessible.onPressAction: row.triggered()

    HoverHandler {
        id: pointer

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: row.triggered()
    }

    Rectangle {
        id: mark

        objectName: "filter-mark-" + row.value
        x: 28
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        radius: 4
        color: row.lit ? Theme.emerald : "transparent"
        border.color: row.lit ? Theme.emerald : Theme.rule
        border.width: row.lit ? 0 : 1
        antialiasing: true

        // Drawn rather than typed: a check mark from a font is a different glyph on every
        // machine, and two of the three are not centred in their own box.
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

    Text {
        objectName: "filter-value-text-" + row.value
        anchors.left: mark.right
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: row.label
        color: row.lit ? Theme.emerald : Theme.ink
        font.family: Theme.textFamily
        font.pixelSize: 14
        font.weight: row.lit ? Font.Medium : Font.Normal
        elide: Text.ElideRight
    }
}
