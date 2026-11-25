// A column of `{label, value}` pairs, as a model hands them over.
//
// A line whose value is not recorded is simply not in the list — « Collection : — » is a line
// that says a fact is missing and takes a line to do it. One pair carries `alarming`, and it
// is the value alone that takes the colour: a filled pill was the only component in a block
// that is otherwise made of nothing but text.
//
// The label is a caption and the value body text, both on `Widths.scale`: at 13 pixels fixed,
// neither shrank at 800 nor followed the desktop's text size (audit, 23/10/2026). Each line
// takes its height from the taller of the two, so a value that wraps at a larger text size
// pushes the next fact down rather than under it.

import QtQuick
import Leaf

Column {
    id: column

    required property var rows

    /// A value that names more people than it shows carries « et N autres » as a link.
    signal linkActivated(string link)

    spacing: Widths.scale.room(4)

    // A count: the page's facts are said again at every mark set on the series, and each
    // saying gave a new list of rows.
    Repeater {
        model: column.rows.length

        Row {
            required property int index
            readonly property var modelData: column.rows[index] ?? ({ "label": "", "value": "" })

            width: column.width
            spacing: Widths.scale.room(10)

            Text {
                id: label

                width: Math.min(Widths.scale.column(132), column.width * 0.42)
                textFormat: Text.PlainText
                text: parent.modelData.label
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(13)
                elide: Text.ElideRight
            }

            Text {
                width: column.width - label.width - parent.spacing
                // Rich only for the rows written for it, which escape what they hold: a
                // publisher spelt « Dupuis & Co » is not markup.
                textFormat: parent.modelData.linked !== undefined ? Text.StyledText
                                                                  : Text.PlainText
                text: parent.modelData.linked ?? parent.modelData.value
                linkColor: Theme.emerald
                onLinkActivated: link => column.linkActivated(link)

                HoverHandler {
                    cursorShape: parent.hoveredLink.length > 0 ? Qt.PointingHandCursor
                                                               : Qt.ArrowCursor
                }
                color: parent.modelData.alarming === true ? Theme.alert : Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.body(13)
                wrapMode: Text.WordWrap
            }
        }
    }
}
