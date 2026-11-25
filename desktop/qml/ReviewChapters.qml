// The chapters a volume lists, on its sheet: how many, said on the button that opens the window
// where they are read and written — a number, a title or a page corrected, a line added, a line
// taken away.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Column {
    id: block

    /// `Review.sheet`.
    required property var sheet

    readonly property var held: block.sheet.chaptersHeld ?? []
    readonly property var words: SheetCaptions.sheetWords

    objectName: "review-chapters"

    // A button with a height of its own, which the keys reach: a `Pill` takes its height from
    // whoever lays it out, and here nobody did — its word was drawn, and a click on it fell
    // through a button nought pixels tall.
    QuietButton {
        objectName: "review-chapters-edit"
        scaled: true
        label: SheetCaptions.chaptersEditLabel(block.held.length)
        enabled: block.sheet.chaptersEditable === true
        onTriggered: window.open()
    }

    ReviewChaptersWindow {
        id: window

        held: block.held
        sheetId: block.sheet.id ?? ""
        volume: block.sheet.title ?? ""
        pages: block.sheet.chaptersPages ?? -1
        arcs: block.sheet.chaptersArcs ?? []
        work: block.sheet.chaptersWork ?? ""
    }
}
