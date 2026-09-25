// Where an arc begins, and how far it runs.
//
// A separator and never a column or a label: an arc is a range, and a line can belong to two
// at once. It claims to contain nothing — the range written beside the name is what says where
// it stops, not the position of the separator below it.
//
// One level of indentation and one only. A saga holds its arcs, and past that the eye loses
// the thread; the model draws the line at one for the same reason.

import QtQuick
import Leaf

Item {
    id: separator

    required property string name
    required property string range
    required property int depth

    objectName: "arc-" + name
    implicitHeight: 30

    Accessible.role: Accessible.Separator
    Accessible.name: separator.name
    Accessible.description: separator.range

    Row {
        x: 6 + separator.depth * 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Text {
            text: separator.name
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: 13
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: separator.range
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: 11
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // What ties an arc to the saga above it. Indentation alone said « further right » and
    // not « inside »: the line is what says inside.
    Rectangle {
        x: 12
        y: 5
        width: 1
        height: parent.height - 8
        visible: separator.depth > 0
        color: Theme.rule
    }

    // The rule sits under the name rather than through it: a separator marks a beginning, and
    // a line across the column would read as an end.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 6 + separator.depth * 18
        height: 1
        color: Theme.rule
    }
}
