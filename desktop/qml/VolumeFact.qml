// One line of a volume's facts: a name and its value, and neither when there is no value.
// The open volume's card and the library's « Informations » say the same things, and drew them
// twice until the second looked nothing like the first.

import QtQuick
import Leaf

Row {
    id: fact

    required property string key
    required property string name
    required property string value
    /// What the parts are named after, for a test to find them.
    property string prefix: "reader-volume-"
    // Holds words, so a column: at a length's rate « Publication » outgrew it as soon as
    // the desktop's text did.
    readonly property int named: Widths.scale.column(78)

    width: parent.width
    spacing: Widths.scale.size(10)
    visible: fact.value.length > 0

    Text {
        objectName: fact.prefix + fact.key + "-name"
        width: fact.named
        textFormat: Text.PlainText
        text: fact.name
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(13)
        font.weight: Font.Medium
    }

    Text {
        objectName: fact.prefix + fact.key
        width: parent.width - fact.named - parent.spacing
        text: fact.value
        color: Theme.ink
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.body(14)
        wrapMode: Text.Wrap
        // Server data, shown as written: a ComicInfo can carry `<b>`, `<br>` or a bare
        // `&`, and the default `AutoText` would read them as markup (see
        // SeriesDescription).
        textFormat: Text.PlainText
    }
}
