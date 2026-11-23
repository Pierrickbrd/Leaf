// The window where the chapters of one volume are written, as the drawing has it: how many and
// over how many pages, a line each — its number, its title, the page it starts on, its ✕ —
// and, between the lines, the arcs of the series that cross them.
//
// The order is the pages' (owner, 04/11/2026): a page typed past the next line's sends its line
// there once the box is left (`Review.edits.chapterPlace`), sliding, and the lines it crosses slide
// the other way. Dragged by a handle, a line could be put where its page said it was not.
//
// A number follows the order: « Numéroter à partir de » numbers every line again from the
// number asked, a line moved by its page numbers them again from the first one's, and
// « + Chapitre » is the last one plus one. Taking a line away leaves the others their numbers:
// a chapter listed twice by mistake is not a reason to renumber a volume. Typed by hand, a
// number was a box per line, and chapters 15 to 22 of a second volume were eight to type.
//
// The arcs are shown and not written here: they run from one volume to another and are written
// on the series (`Review.sheet.chaptersWork`).
//
// Edited on a copy and kept only by « Garder les chapitres », as the arcs are: a line half typed
// is a page that is no number for a moment. The model has the last word (`Review.edits.setChapters`)
// and the window says so rather than closing on a refusal. A line that was there keeps what the
// window does not show (`source`), so a correction never costs a chapter its other keys.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Leaf

LeafDialog {
    id: window

    property var held: []
    property string sheetId: ""
    property string volume: ""
    property int pages: -1
    /// `Review.sheet.chaptersArcs`: `{at, name, state, said, starts}`, each over line `at`.
    property var arcs: []
    /// The series' sheet, where the arcs are written; empty when the volume has none.
    property string work: ""
    property bool refused: false

    /// The lines being edited: a model and not an array, so that a line moved by its page is
    /// one line sliding, and not the whole list drawn again under the hand.
    ListModel {
        id: rows
    }

    readonly property var words: SheetCaptions.sheetWords

    // The widths of the columns, shared by the head and every line so that they line up.
    readonly property int numberWidth: Widths.scale.fit(36)
    readonly property int pageWidth: Widths.scale.fit(84)
    readonly property int removeWidth: Widths.scale.size(32)
    readonly property int gap: Widths.space.row
    readonly property real titleWidth: Math.max(0, width - leftPadding - rightPadding
                                                   - numberWidth - pageWidth - removeWidth
                                                   - 3 * gap - Widths.space.compact)

    function open() {
        rows.clear()
        for (const one of window.held)
            rows.append({ number: one.number, title: one.title, page: one.page, source: one.source })
        from.text = rows.count > 0 ? rows.get(0).number : "1"
        window.refused = false
        window.visible = true
    }

    /// The lines as `Review` reads them.
    function listed(): var {
        const out = []
        for (let at = 0; at < rows.count; ++at) {
            const one = rows.get(at)
            out.push({ number: one.number, title: one.title, page: one.page, source: one.source })
        }
        return out
    }

    /// Every line numbered again from `first`, in place.
    function renumber(first: string) {
        const numbered = Review.edits.chaptersNumbered(window.listed(), first)
        for (let at = 0; at < numbered.length; ++at)
            rows.setProperty(at, "number", numbered[at].number)
    }

    /// The line at `at` sent where its page puts it, and the lines numbered again from the
    /// first one's number.
    function place(at: int) {
        const to = Review.edits.chapterPlace(window.listed(), at)
        if (to === at)
            return
        const first = rows.get(0).number
        rows.move(at, to, 1)
        window.renumber(first)
    }

    /// A line at the end, numbered the last one plus one — the first number asked when the
    /// list is empty.
    function add() {
        const last = rows.count > 0 ? Number(String(rows.get(rows.count - 1).number).replace(",", "."))
                                    : NaN
        const next = isNaN(last) ? (from.text.length > 0 ? from.text : "1")
                                 : Review.edits.chaptersNumbered([{}], String(Math.floor(last) + 1))[0].number
        rows.append({ number: next, title: "", page: "", source: -1 })
        Qt.callLater(() => scroller.contentY = Math.max(0, scroller.contentHeight - scroller.height))
    }

    objectName: "review-chapters-window"
    surfaceName: "review-chapters-surface"
    drawnWidth: 820

    Item {
        width: parent.width
        height: Math.max(heading.implicitHeight, commandsOnTop.height)

        Text {
            id: heading

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            // Never under the commands: the name of a volume is a long title, and the count that
            // takes what is left of the line went to nothing before the title gave way.
            width: Math.min(implicitWidth, parent.width - commandsOnTop.width
                                           - 2 * Widths.space.row)
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: SheetCaptions.chaptersWindowTitle(window.volume)
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: Widths.scale.title(20)
            font.weight: Font.Bold
        }

        Text {
            objectName: "review-chapters-count"
            anchors.left: heading.right
            anchors.leftMargin: Widths.space.row
            anchors.right: commandsOnTop.left
            anchors.rightMargin: Widths.space.row
            anchors.baseline: heading.baseline
            textFormat: Text.PlainText
            text: SheetCaptions.chaptersCountLabel(rows.count, window.pages)
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(13)
            elide: Text.ElideRight
        }

        Row {
            id: commandsOnTop

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Widths.space.compact

            QuietButton {
                objectName: "review-chapters-renumber"
                scaled: true
                label: window.words.chaptersNumberFrom
                enabled: rows.count > 0
                onTriggered: window.renumber(from.text)
            }

            LeafField {
                id: from

                objectName: "review-chapters-from"
                scaled: true
                anchors.verticalCenter: parent.verticalCenter
                width: Widths.scale.fit(56)
                // The height of the two pills it stands between: at its own 34 it stood a pixel
                // above and below both.
                height: Widths.scale.fit(32)
                activeFocusOnTab: true
                Accessible.name: window.words.chaptersNumberFrom
            }

            ActionButton {
                objectName: "review-chapter-add"
                scaled: true
                label: window.words.chaptersAdd
                onTriggered: window.add()
            }
        }
    }

    Row {
        spacing: window.gap

        component Head: Text {
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(11)
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.6
        }

        Head { width: window.numberWidth; text: window.words.chaptersNumber }
        Head { width: window.titleWidth; text: window.words.chaptersTitle }
        Head { width: window.pageWidth; text: window.words.chaptersPage }
    }

    Flickable {
        id: scroller

        width: parent.width
        height: Math.min(contentHeight, Widths.scale.fit(420))
        contentHeight: lines.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: lines

            width: scroller.width - Widths.space.compact

            // A line sent by its page slides to its place, and the ones it crosses the other way.
            move: Transition {
                NumberAnimation {
                    property: "y"
                    duration: Preferences.motion === Preferences.Slide ? 230 : 1
                    easing.type: Easing.OutCubic
                }
            }

            Repeater {
                model: rows

                delegate: Column {
                    id: chapter

                    required property int index
                    required property string number
                    required property string title
                    required property string page

                    width: lines.width

                    // The arcs that cross the volume at this line: one already running over
                    // the first, one opening over the line of the chapter it opens at.
                    Repeater {
                        model: window.arcs.filter(one => one.at === chapter.index)

                        delegate: Item {
                            id: band

                            required property var modelData
                            required property int index

                            objectName: "review-chapters-arc-" + chapter.index + "-" + band.index
                            width: lines.width
                            height: arcName.implicitHeight + Widths.space.row + Widths.space.tight

                            Text {
                                id: arcWord

                                anchors.left: parent.left
                                anchors.baseline: arcName.baseline
                                textFormat: Text.PlainText
                                text: window.words.chaptersArc
                                color: band.modelData.starts ? Theme.inkSoft : Theme.emerald
                                font.family: Theme.textFamily
                                font.pixelSize: Widths.scale.caption(10)
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 0.6
                            }

                            Text {
                                id: arcName

                                anchors.left: arcWord.right
                                anchors.leftMargin: Widths.space.row
                                anchors.right: arcSaid.left
                                anchors.rightMargin: Widths.space.row
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: Widths.space.tight
                                textFormat: Text.PlainText
                                text: band.modelData.name + " · " + band.modelData.state
                                color: band.modelData.starts ? Theme.inkSoft : Theme.emerald
                                font.family: Theme.textFamily
                                font.pixelSize: Widths.scale.caption(12)
                                elide: Text.ElideRight
                            }

                            Text {
                                id: arcSaid

                                anchors.right: parent.right
                                anchors.baseline: arcName.baseline
                                textFormat: Text.PlainText
                                text: band.modelData.said
                                color: Theme.inkSoft
                                font.family: Theme.textFamily
                                font.pixelSize: Widths.scale.caption(12)
                            }
                        }
                    }

                    Item {
                        width: lines.width
                        height: Math.max(title.implicitHeight, remove.height) + 2 * Widths.space.tight

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 1
                            color: Theme.rule
                        }

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: window.gap

                            Text {
                                objectName: "review-chapter-number-" + chapter.index
                                width: window.numberWidth
                                anchors.verticalCenter: parent.verticalCenter
                                textFormat: Text.PlainText
                                text: chapter.number
                                color: Theme.ink
                                font.family: Theme.textFamily
                                font.pixelSize: Widths.scale.body(13)
                                font.weight: Font.Bold
                            }

                            LeafField {
                                id: title

                                objectName: "review-chapter-title-" + chapter.index
                                scaled: true
                                width: window.titleWidth
                                activeFocusOnTab: true
                                text: chapter.title
                                placeholderText: window.words.chaptersTitle
                                onTextEdited: rows.setProperty(chapter.index, "title", text)
                            }

                            LeafField {
                                objectName: "review-chapter-page-" + chapter.index
                                scaled: true
                                width: window.pageWidth
                                activeFocusOnTab: true
                                text: chapter.page
                                placeholderText: window.words.chaptersPage
                                validator: IntValidator { bottom: 0 }
                                onTextEdited: rows.setProperty(chapter.index, "page", text)
                                // Placed once left, and not at each figure: « 1 » then « 12 »
                                // then « 120 » was the line crossing the list three times.
                                onEditingFinished: Qt.callLater(window.place, chapter.index)
                            }

                            IconButton {
                                id: remove

                                objectName: "review-chapter-remove-" + chapter.index
                                anchors.verticalCenter: parent.verticalCenter
                                width: window.removeWidth
                                height: width
                                side: Widths.scale.icon(16)
                                glyph: "close"
                                label: window.words.arcsRemove
                                activeFocusOnTab: true
                                onTriggered: rows.remove(chapter.index)
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        visible: window.work.length > 0
        width: parent.width
        height: note.implicitHeight + 2 * Widths.space.row
        radius: Widths.scale.size(10)
        color: Theme.onPaper
        border.color: Theme.rule
        border.width: 1

        Row {
            id: note

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Widths.space.row
            anchors.verticalCenter: parent.verticalCenter
            spacing: Widths.space.row

            Text {
                width: parent.width - arcsEdit.width - parent.spacing
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: window.words.chaptersArcsNote
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                wrapMode: Text.WordWrap
            }

            QuietButton {
                id: arcsEdit

                objectName: "review-chapters-arcs"
                anchors.verticalCenter: parent.verticalCenter
                scaled: true
                label: window.words.chaptersArcsEdit
                onTriggered: {
                    window.visible = false
                    Review.view.select(window.work)
                }
            }
        }
    }

    Text {
        width: parent.width
        visible: window.refused
        textFormat: Text.PlainText
        text: window.words.chaptersRefused
        color: Theme.alert
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        wrapMode: Text.WordWrap
    }

    commands: [
        LeafTextAction {
            objectName: "review-chapters-cancel"
            scaled: true
            label: ImportCaptions.cancelLabel
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: window.visible = false
        },

        ActionButton {
            objectName: "review-chapters-keep"
            scaled: true
            label: window.words.chaptersKeep
            onTriggered: {
                if (Review.edits.setChapters(window.sheetId, window.listed()))
                    window.visible = false
                else
                    window.refused = true
            }
        }
    ]
}
