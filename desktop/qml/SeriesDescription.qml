// What the header did not take: the summary whole, the civil status, and — apart — what this
// library holds of the edition.
//
// Not the genres: the header carries them, and the foot of this tab drew the same pills a
// second time. On 23/10/2026 the owner chose one place for them, and the header is the one
// seen without opening a tab.
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
    /// « et N autres » pressed: the window listing everyone the series is credited with.
    signal peopleAsked()
    required property var facts

    readonly property var credits: description.facts.credits ?? []
    readonly property var nature: description.facts.nature ?? []
    readonly property var holding: description.facts.holding ?? []
    readonly property var received: description.facts.received ?? []

    /// The measure of the summary: two and a half lowercase alphabets of its own font, the
    /// printer's rule for a line of about seventy characters, whatever the window. It was 900
    /// pixels, the one reading bound this client had, and 900 pixels of 14-pixel text is a
    /// hundred and thirty characters a line — twice what the eye carries back to the next one
    /// (audit, 23/10/2026). Read from the font, so it widens with the desktop's text size and
    /// with nothing else. Not the font's average character width: that figure counts capitals
    /// and wide glyphs prose rarely uses, and seventy of them laid out ninety-six characters.
    ///
    /// The size is read before the call: `advanceWidth` is a function, and a binding re-reads
    /// a function only when a property it read notifies — without it the measure stayed the
    /// one of the first font, and a larger text size laid fifty-six characters a line.
    readonly property int measure: prose.font.pixelSize > 0
                                   ? Math.ceil(prose.advanceWidth("abcdefghijklmnopqrstuvwxyz")
                                               * 2.5)
                                   : 0
    /// The width of everything under the summary: the facts and the card. At 1920 the facts
    /// ran the window's width, their second column starting beyond the summary's right edge,
    /// and the card held one line of two hundred pixels across 1888 of them. Narrower than
    /// the page's column (`Widths.space.column`) on purpose: two columns of facts are read
    /// with the summary above them, and are held near its measure rather than the page's.
    readonly property int span: Math.min(width, Widths.scale.column(800))
    /// The narrowest a column of facts can be and still hold a label and a value beside it.
    /// Two columns when two of these fit in the span, one when they do not — the width the
    /// columns really have, and not a band: at 800 the band was Medium and the two columns
    /// were 366 pixels each, the values wrapping under their own labels.
    readonly property int factColumn: Widths.scale.column(132) + Widths.scale.room(10)
                                      + Widths.scale.column(200)
    readonly property int factGap: Widths.scale.room(36)

    function factColumnsIn(width) {
        return width >= 2 * description.factColumn + description.factGap ? 2 : 1
    }

    objectName: "series-description"
    // Three sections — the story, what the work is, what you hold of it — and so a section
    // apart, as on the last tab and in the settings: room(16) was a block's gap between
    // sections (spacing audit, 23/10/2026).
    spacing: Widths.space.section

    // First and whole: the one thing anybody opens this tab to read. Held to a measure,
    // because a line of prose the width of a wide window is a line whose next beginning the
    // eye cannot find.
    Text {
        id: summaryText

        objectName: "series-summary"
        width: Math.min(parent.width, description.measure)
        text: description.summary
        visible: text.length > 0
        color: Theme.ink
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.body(14)
        lineHeight: 1.45
        wrapMode: Text.WordWrap
        // Said out loud, because the default is not this. `Text` starts at `AutoText`, which
        // asks `mightBeRichText()` of whatever it is given — and this is given prose from a
        // file on disk, a `work.json` edited by hand or a `ComicInfo.xml` written by somebody
        // else's scraper, where a stray `<i>` is ordinary. Read as rich text it would collapse
        // the blank line between the story and the printing, which is the one piece of shape
        // these summaries have, and it would let `<img src>` fetch whatever it names.
        textFormat: Text.PlainText

        FontMetrics {
            id: prose

            font: summaryText.font
        }
    }

    // Two columns: who made it and who publishes it on the left, what it is on the right —
    // and one column when the width is too narrow to hold two without the values drifting
    // away from their labels. The card below collapses its own at the same rule.
    Grid {
        objectName: "series-facts"
        width: description.span
        columns: description.factColumnsIn(width)
        columnSpacing: description.factGap
        rowSpacing: Widths.scale.room(10)

        readonly property real side: columns === 1 ? width : (width - columnSpacing) / 2

        FactColumn {
            objectName: "facts-credits"
            width: parent.side
            rows: description.credits
            onLinkActivated: description.peopleAsked()
        }

        FactColumn {
            objectName: "facts-nature"
            width: parent.side
            rows: description.nature
        }
    }

    // The one block that speaks about *you* rather than about the work, which is why it is
    // on a card of its own — and the only place that says a volume is missing.
    Card {
        objectName: "series-holding"
        width: description.span
        visible: description.holding.length > 0

        // The card's first line, `row` above the next like every line of a card: the gap a
        // heading leaves above a section is for content outside a surface.
        SectionHeading {
            width: parent.width
            text: SeriesCaptions.inThisLibrary
            bottomPadding: 0
        }

        // Two columns like the pair above it: what is on the shelf on the left, when it
        // arrived on the right. Stacked in one, the dates read as two more facts about the
        // collection rather than as a history of it.
        Grid {
            objectName: "holding-facts"
            width: parent.width
            columns: description.factColumnsIn(width)
            columnSpacing: description.factGap
            rowSpacing: Widths.scale.room(10)

            readonly property real side: columns === 1 ? width : (width - columnSpacing) / 2

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
