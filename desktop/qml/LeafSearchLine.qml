// A field for searching inside one long axis.
//
// Not the application's search field and deliberately not shaped like it: this narrows a list
// already on screen, where that one asks the server a question. A reader who cannot tell them
// apart will type a series name in here and conclude the library is empty.

import QtQuick
import QtQuick.Controls
import Leaf

TextField {
    id: line

    required property string placeholder

    signal asked(string text)

    height: 30
    activeFocusOnTab: false
    placeholderText: Captions.searchWithin(placeholder)
    color: Theme.ink
    placeholderTextColor: Theme.inkFaint
    selectionColor: Theme.emerald
    selectedTextColor: Theme.onEmerald
    font.family: Theme.textFamily
    font.pixelSize: 13
    leftPadding: 10
    rightPadding: 10
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter

    onTextEdited: line.asked(text)

    background: Rectangle {
        radius: 6
        color: Theme.onBar
        border.color: line.activeFocus ? Theme.emerald : "transparent"
        border.width: 1
    }
}
