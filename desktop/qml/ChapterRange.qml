// One stretch of chapters inside the volume above.
//
// Two of these and a separator between them are how a file an arc runs through says where the
// frontier falls. Not the chapters themselves: a range is not something one opens, so the
// `startPage` most libraries do not carry is missed by nobody — and a file is opened whole or
// not at all.

import QtQuick
import Leaf

Item {
    id: stretch

    required property string range

    objectName: "range-" + range
    implicitHeight: 22

    Accessible.role: Accessible.StaticText
    Accessible.name: stretch.range

    // Tied to the line above by a hairline rather than by indentation alone: it belongs to
    // that file, and nothing else in the column is a child of anything.
    Rectangle {
        x: 22
        width: 1
        height: parent.height
        color: Theme.rule
    }

    Text {
        x: 34
        anchors.verticalCenter: parent.verticalCenter
        text: stretch.range
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: 11
    }
}
