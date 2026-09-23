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
    /// The four columns in one map, as `Series` hands them over. Unpacked here rather than
    /// in the page above: a page still loading answers with an empty map, and `?? []` written
    /// once is `?? []` that cannot be forgotten at the fifth reader.
    required property var facts
    required property var genres

    readonly property var credits: description.facts.credits ?? []
    readonly property var nature: description.facts.nature ?? []
    readonly property var holding: description.facts.holding ?? []
    readonly property var received: description.facts.received ?? []

    objectName: "series-description"
    spacing: 16

    // First and whole: the one thing anybody opens this tab to read. Held to a measure,
    // because a line of prose the width of a wide window is a line whose next beginning the
    // eye cannot find.
    Text {
        width: Math.min(parent.width, 900)
        text: description.summary
        visible: text.length > 0
        color: Theme.ink
        font.family: Theme.textFamily
        font.pixelSize: 14
        lineHeight: 1.45
        wrapMode: Text.WordWrap
        // Said out loud, because the default is not this. `Text` starts at `AutoText`, which
        // asks `mightBeRichText()` of whatever it is given — and this is given prose from a
        // file on disk, a `work.json` edited by hand or a `ComicInfo.xml` written by somebody
        // else's scraper, where a stray `<i>` is ordinary. Read as rich text it would collapse
        // the blank line between the story and the printing, which is the one piece of shape
        // these summaries have, and it would let `<img src>` fetch whatever it names.
        textFormat: Text.PlainText
    }

    // Two columns: who made it and who publishes it on the left, what it is on the right —
    // and one column when the window is too narrow to hold two without the values drifting
    // away from their labels. The design collapses them at the same point.
    Grid {
        width: parent.width
        columns: Widths.band === Widths.Narrow ? 1 : 2
        columnSpacing: 36
        rowSpacing: 10

        readonly property real side: columns === 1 ? width : (width - columnSpacing) / 2

        FactColumn {
            width: parent.side
            rows: description.credits
        }

        FactColumn {
            width: parent.side
            rows: description.nature
        }
    }

    // The same pills the header carries, closing the civil status they belong to. Said in
    // both places on purpose: up there they answer « what kind of book is this » at a glance,
    // down here they are the last line of what the work *is*.
    Flow {
        width: parent.width
        spacing: 6

        Repeater {
            model: description.genres

            Rectangle {
                required property string modelData

                radius: 99
                color: Theme.onPaper
                implicitWidth: said.implicitWidth + 22
                implicitHeight: 25

                Text {
                    id: said

                    anchors.centerIn: parent
                    text: parent.modelData
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 12
                }
            }
        }
    }

    // The one block that speaks about *you* rather than about the work, which is why it is
    // on a card of its own — and the only place that says a volume is missing.
    Rectangle {
        width: parent.width
        height: held.implicitHeight + 28
        visible: description.holding.length > 0
        radius: Theme.cardRadius
        color: Theme.surface

        // The same lift every card in this client is laid on the paper with.
        CardLift { level: CardLift.Card }

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
                font.pixelSize: 12
                font.capitalization: Font.AllUppercase
            }

            // Two columns like the pair above it: what is on the shelf on the left, when it
            // arrived on the right. Stacked in one, the dates read as two more facts about
            // the collection rather than as a history of it.
            Grid {
                width: parent.width
                columns: Widths.band === Widths.Narrow ? 1 : 2
                columnSpacing: 36
                rowSpacing: 10

                readonly property real side: columns === 1 ? width
                                                           : (width - columnSpacing) / 2

                FactColumn {
                    width: parent.side
                    rows: description.holding
                }

                FactColumn {
                    width: parent.side
                    rows: description.received
                }
            }
        }
    }
}
