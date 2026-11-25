// A name above a field to type in, and the hint under it when there is one: the shape both
// editing windows give each of their lines.

import QtQuick
import QtQuick.Controls
import Leaf

Column {
    id: line

    required property string name
    property alias text: field.text
    property string hint: ""
    /// What is wrong with what is typed, said under the field as it becomes wrong: a button
    /// that stays grey with no word beside it leaves the person guessing which line.
    property string problem: ""
    property alias field: field
    property alias inputMethodHints: field.inputMethodHints

    width: parent.width
    spacing: Widths.space.compact

    Text {
        textFormat: Text.PlainText
        text: line.name
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        font.weight: Font.Medium
    }

    LeafField {
        id: field

        objectName: line.objectName + "-field"
        width: parent.width
        activeFocusOnTab: true
        placeholderText: line.hint
    }

    Text {
        objectName: line.objectName + "-problem"
        width: parent.width
        visible: line.problem.length > 0
        textFormat: Text.PlainText
        text: line.problem
        color: Theme.alert
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        wrapMode: Text.WordWrap

        Accessible.role: Accessible.AlertMessage
        Accessible.name: text
    }
}
