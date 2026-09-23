// A field to type in, in the client's own colours.
//
// The colours and the edge, and nothing about what is typed: `LeafSearchLine` narrows a list
// with it and the deletion of an edition asks for a name to be typed into it. Those two do
// not share a placeholder, a signal or a purpose — only a shape, which is exactly what lives
// here.

import QtQuick
import QtQuick.Controls
import Leaf

TextField {
    id: field

    height: 34
    activeFocusOnTab: false
    color: Theme.ink
    placeholderTextColor: Theme.inkFaint
    selectionColor: Theme.emerald
    selectedTextColor: Theme.onEmerald
    font.family: Theme.textFamily
    font.pixelSize: 14
    leftPadding: 10
    rightPadding: 10
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter

    background: Rectangle {
        radius: 6
        color: Theme.onBar
        border.color: field.activeFocus ? Theme.emerald : "transparent"
        border.width: 1
    }
}
