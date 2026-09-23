// What the header did not take: the summary whole, the civil status, and — apart — what this
// library holds of the edition.
//
// The two are not the same fact, and `Api.h` says so already: « Holding separates what this
// library holds of an edition from what the edition is ». So the second sits on a card of its
// own, and it is the only place that says a volume is missing.

import QtQuick
import Leaf

Column {
    id: description

    required property string summary
    required property var credits
    required property var nature
    required property var holding

    objectName: "series-description"
    spacing: 16

    // First and whole: the one thing anybody opens this tab to read.
    Text {
        width: parent.width
        text: description.summary
        visible: text.length > 0
        color: Theme.ink
        font.family: Theme.textFamily
        font.pixelSize: 13
        lineHeight: 1.45
        wrapMode: Text.WordWrap
    }

    // Two columns: who made it and who publishes it on the left, what it is on the right.
    Row {
        width: parent.width
        spacing: 24

        FactColumn {
            width: (parent.width - parent.spacing) / 2
            rows: description.credits
        }

        FactColumn {
            width: (parent.width - parent.spacing) / 2
            rows: description.nature
        }
    }

    Rectangle {
        width: parent.width
        height: held.implicitHeight + 28
        visible: description.holding.length > 0
        radius: Theme.cardRadius
        color: Theme.surface

        Column {
            id: held

            x: 16
            y: 14
            width: parent.width - 32
            spacing: 10

            Text {
                text: SeriesCaptions.inThisLibrary
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: 11
                font.capitalization: Font.AllUppercase
            }

            FactColumn { width: parent.width; rows: description.holding }
        }
    }
}
