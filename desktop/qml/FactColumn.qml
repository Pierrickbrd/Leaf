// A column of `{label, value}` pairs, as a model hands them over.
//
// A line whose value is not recorded is simply not in the list — « Collection : — » is a line
// that says a fact is missing and takes a line to do it. One pair carries `alarming`, and it
// is the value alone that takes the colour: a filled pill was the only component in a block
// that is otherwise made of nothing but text.

import QtQuick
import Leaf

Column {
    id: column

    required property var rows

    spacing: 4

    Repeater {
        model: column.rows

        Row {
            required property var modelData

            width: column.width
            spacing: 10

            Text {
                id: label

                width: Math.min(110, column.width * 0.42)
                text: parent.modelData.label
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            Text {
                width: column.width - label.width - parent.spacing
                text: parent.modelData.value
                color: parent.modelData.alarming === true ? Theme.alert : Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }
}
