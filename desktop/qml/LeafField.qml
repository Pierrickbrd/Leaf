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

    /// Edged in `rule` while it is not focused, for a field standing on a surface of its own
    /// colour: in the dark theme `onBar` and `onPaper` are one, and the field vanished.
    property bool edged: false
    /// No fill and no edge of its own, for a field standing inside a box that draws them —
    /// the names of `TokenField` framed with their pills.
    property bool bare: false
    /// Follows the window as `LeafSelect` does, which stands beside it in a row: at 1920 the
    /// select grew to 43 and the fields next to it stayed at 34, hung from the top of the row.
    /// Off by default: the pages that hold a field of their own size do not move.
    property bool scaled: false

    function sized(drawn: real): int {
        return field.scaled ? Widths.scale.size(drawn) : drawn
    }

    height: field.sized(34)
    activeFocusOnTab: false
    color: Theme.ink
    placeholderTextColor: Theme.inkFaint
    selectionColor: Theme.emerald
    selectedTextColor: Theme.onEmerald
    font.family: Theme.textFamily
    font.pixelSize: field.scaled ? Widths.scale.body(14) : 14
    // Room round a word follows the word: a field of a `fit` width kept its text at 1920.
    leftPadding: field.scaled ? Widths.scale.fit(10) : 10
    rightPadding: field.scaled ? Widths.scale.fit(10) : 10
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter

    background: Rectangle {
        radius: field.sized(6)
        color: field.bare ? "transparent" : Theme.onBar
        border.color: field.bare ? "transparent"
                      : field.activeFocus ? Theme.emerald : field.edged ? Theme.rule : "transparent"
        border.width: 1
    }
}
