// What the open volume is, on a card over its pages.
//
// A volume has no page of its own in this application (spec §06): there is the series, and
// there is the book being read, and what one wants to know about the second — how many pages,
// which chapters, when it came out, how far one has got — is wanted while reading it. So it
// is a card laid over the right of the pages, which keep the whole window under it (see
// `ReaderView`, where it slides in and out). This only draws the card.
//
// **A field the volume does not have has no line.** `Reader.volume` leaves a key out rather
// than hold it empty, and a line here is shown only for a value that is there: a label stood
// over nothing reads as a fault in the library, when it is only a book without an ISBN.
//
// **The list of chapters is the list of the bar's marks**, in the same order and with the
// same « current » — a chapter that does not say where it begins is listed and cannot be
// clicked, since jumping to « I do not know where » is not jumping to the cover.
//
// **It opens at its top**, on the cover and the facts, and lights the chapter being read
// without scrolling to it. Opened on the current chapter, it hid what one opens it to see —
// what the volume is — under a list scrolled half-way (owner's test of 22/10/2026). It
// follows the reader only once open, from one chapter into the next.
//
// **It follows the window as the dock does** (`Widths.scale`): every size below, words, cover
// and rows included, is drawn for a window of 1280 and scaled with it, its words never under
// their floor. In the owner's test of 22/10/2026 on a full-screen window of 1920, the dock had
// grown and this card had kept its drawn size beside it. Too narrow for the facts beside the
// cover, it puts the cover above them.
//
// **An ISBN that adds up is a key** (`IsbnKey`), the owner's choice of 22/10/2026, and the
// last thing in the sheet, after the chapters, scrolled with them. Anchored at the foot of the
// card under the sheet, as it first was, it took the bottom of the card from the list: in the
// owner's test of 22/10/2026 the seventeenth chapter of Erased 3 was cut in half behind it.
// An ISBN that does not add up stays the plain line it was — a barcode drawn over a typo would
// certify the typo.
//
// Every word is `ReaderCaptions`'; this lays them out.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Rectangle {
    id: card

    readonly property int padding: Widths.scale.size(18)
    /// The cover above the facts and not beside them: beside a cover, a card narrower than
    /// this left the facts a column of one word per line.
    readonly property bool stacked: card.width - 2 * card.padding < Widths.scale.column(380)
    /// What the open volume has, key by key — see `Reader.volume`.
    readonly property var about: Reader.volume
    /// The ISBN as bars and lines, or empty when there is none or it does not add up.
    readonly property var barcode: Isbn.barcode(card.about.isbn || "")
    readonly property bool barcoded: !!card.barcode.digits

    /// Which chapter is being read, −1 for none. Its own property, and not read inside
    /// `reveal`, so that the list follows the reader into the next chapter and only then:
    /// `Reader.chapters` changes at every page turned, and scrolling back to the chapter at
    /// each of them would take the list out of the hand of whoever is looking down it.
    readonly property int at: {
        const all = Reader.chapters
        for (let i = 0; i < all.length; ++i) {
            if (all[i].current)
                return i
        }
        return -1
    }
    /// The chapter `at` last held, and the book it held it in: the list follows a move from
    /// one lit chapter to another of the same book, and nothing else. A book opened, or its
    /// chapters arriving after it, is a card seen for the first time, and starts at its top.
    property int followed: -1
    property string followedIn: ""
    onAtChanged: {
        const moved = card.followed >= 0 && card.at >= 0
                      && card.followedIn === Reader.entryId
        card.followed = card.at
        card.followedIn = Reader.entryId
        if (moved && card.visible)
            card.reveal()
    }
    /// Another book under the open card: its own cover, at the top.
    readonly property string entry: Reader.entryId
    onEntryChanged: sheet.contentY = 0

    /// The cross was pressed: the card asks to be closed, as Escape does.
    signal closeRequested()

    /// Brings the chapter being read into view, when the reader has gone into it with the
    /// card open: the lit row would otherwise leave the list for one nobody sees.
    function reveal() {
        // The columns are laid out lazily, at the next frame: asked at once, a row would
        // report the place it had before the list was built, and the card would scroll to
        // a stale position with nothing to correct it. Laid out now, the answer is true.
        content.forceLayout()
        list.forceLayout()
        const row = card.at < 0 ? null : rows.itemAt(card.at)
        if (!row)
            return
        const top = row.mapToItem(content, 0, 0).y
        const room = sheet.height
        sheet.contentY = Math.max(0, Math.min(sheet.contentHeight - room,
                                              top - (room - row.height) / 2))
    }

    objectName: "reader-volume"
    radius: Widths.scale.size(Theme.cardRadius)
    color: Theme.surface
    border.color: Theme.rule
    border.width: 1
    antialiasing: true

    // Opened, the card is at its top: it is built once with the screen and only shown and
    // hidden, so a list scrolled down last time would otherwise be where it opens next.
    onVisibleChanged: {
        if (card.visible)
            sheet.contentY = 0
    }

    CardLift { level: CardLift.Menu }

    /// One line of the facts: a name and its value, and neither when there is no value.
    component Fact: Row {
        id: fact

        required property string key
        required property string name
        required property string value
        readonly property int named: Widths.scale.size(78)

        width: parent.width
        spacing: Widths.scale.size(10)
        visible: fact.value.length > 0

        Text {
            objectName: "reader-volume-" + fact.key + "-name"
            width: fact.named
            text: fact.name
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(13)
            font.weight: Font.Medium
        }

        Text {
            objectName: "reader-volume-" + fact.key
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

    Item {
        id: head

        x: card.padding
        y: card.padding
        width: card.width - 2 * card.padding
        height: Widths.scale.size(26)

        Text {
            objectName: "reader-volume-head"
            anchors.left: parent.left
            anchors.right: cross.left
            anchors.rightMargin: Widths.scale.size(8)
            anchors.verticalCenter: parent.verticalCenter
            text: ReaderCaptions.volumeHead(Reader.entryLabel, card.about.title || "")
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: Widths.scale.title(17)
            font.weight: Font.Bold
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }

        IconButton {
            id: cross

            objectName: "reader-volume-close"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Widths.scale.size(26)
            height: width
            side: Widths.scale.icon(20)
            glyph: "close"
            label: ReaderCaptions.closeCard
            onTriggered: card.closeRequested()
        }
    }

    Flickable {
        id: sheet

        objectName: "reader-volume-sheet"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: head.bottom
        anchors.topMargin: Widths.scale.size(12)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: card.padding
        contentWidth: width
        // Room under the key for its lip's shadow, which the sheet's clip would cut.
        contentHeight: content.height + (card.barcoded ? Widths.scale.size(8) : 0)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content

            x: card.padding
            width: sheet.width - 2 * card.padding
            spacing: Widths.scale.size(14)

            Grid {
                objectName: "reader-volume-about"
                width: parent.width
                columns: card.stacked ? 1 : 2
                columnSpacing: Widths.scale.size(16)
                rowSpacing: Widths.scale.size(14)

                Item {
                    id: cover

                    width: Widths.scale.size(112)
                    height: Widths.scale.size(168)

                    // Only fetched once the card is shown: the card lives as long as the
                    // reader does, and a cover asked for at every opening of a book nobody
                    // looked at is a request for nothing.
                    RoundedCover {
                        objectName: "reader-volume-cover"
                        anchors.fill: parent
                        radius: Widths.scale.size(Theme.coverRadius)
                        source: card.visible && card.about.cover ? card.about.cover : ""
                    }

                    CoverSkin {
                        radius: Widths.scale.size(Theme.coverRadius)
                    }
                }

                Column {
                    objectName: "reader-volume-facts"
                    width: card.stacked ? parent.width
                                        : parent.width - cover.width - parent.columnSpacing
                    spacing: Widths.scale.size(7)

                    Fact {
                        key: "series"
                        name: ReaderCaptions.factSeries
                        value: Reader.seriesName
                    }

                    Fact {
                        key: "pages"
                        name: ReaderCaptions.factPages
                        value: card.about.pageCount > 0 ? String(card.about.pageCount) : ""
                    }

                    Fact {
                        key: "chapters"
                        name: ReaderCaptions.factChapters
                        value: card.about.chapters || ""
                    }

                    Fact {
                        key: "published"
                        name: ReaderCaptions.factPublished
                        value: card.about.publishedOn
                               ? ReaderCaptions.published(card.about.publishedOn) : ""
                    }

                    Fact {
                        key: "isbn"
                        name: ReaderCaptions.factIsbn
                        // Drawn as the key after the chapters when it adds up; the line is
                        // what is left for one that does not.
                        value: card.barcoded ? "" : card.about.isbn || ""
                    }

                    Fact {
                        key: "read"
                        name: ReaderCaptions.factRead
                        value: card.about.started || card.about.finished
                               ? ReaderCaptions.readAt(card.about.page, card.about.pageCount,
                                                       !!card.about.finished)
                               : ""
                    }

                    // The shelf's bar, under « Lu » as the design draws it. Laid on
                    // paper and not on a cover, so without the shade the bar keeps for a green
                    // cover: under the translucent track it only made it darker than drawn.
                    Item {
                        objectName: "reader-volume-read-bar"
                        width: parent.width
                        height: visible ? 4 : 0
                        visible: !!card.about.started || !!card.about.finished

                        Item {
                            x: Widths.scale.size(88)
                            width: parent.width - x
                            height: 4

                            ReadBar {
                                key: Reader.entryId
                                shaded: false
                                started: !!card.about.started
                                finished: !!card.about.finished
                                howFarRead: card.about.howFarRead || 0
                            }
                        }
                    }
                }
            }

            Text {
                objectName: "reader-volume-summary"
                width: parent.width
                visible: text.length > 0
                text: card.about.summary || ""
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.body(14)
                wrapMode: Text.Wrap
                lineHeight: 1.2
                textFormat: Text.PlainText
            }

            Text {
                objectName: "reader-volume-chapters-heading"
                visible: Reader.chapters.length > 0
                text: ReaderCaptions.factChapters
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                font.weight: Font.Medium
                font.capitalization: Font.AllUppercase
                font.letterSpacing: Widths.scale.letters(0.8)
            }

            Column {
                id: list

                width: parent.width

                Repeater {
                    id: rows

                    // The number of chapters and not the list: the list is a new one at every
                    // page turned, because only `current` moves, and a model that is a new
                    // list rebuilds every row each time. A count changes with the volume.
                    model: Reader.chapters.length

                    Item {
                        id: row

                        required property int index
                        readonly property var modelData: Reader.chapters[row.index]

                        readonly property bool current: !!row.modelData && row.modelData.current
                        readonly property bool jumps: !!row.modelData && row.modelData.startPage >= 0

                        objectName: "reader-volume-chapter-" + row.index
                        width: parent.width
                        height: Math.max(Widths.scale.size(34),
                                         label.implicitHeight + Widths.scale.size(14))

                        Rectangle {
                            anchors.fill: parent
                            radius: Widths.scale.size(8)
                            color: row.current ? Theme.emeraldWash
                                               : (pointer.hovered && row.jumps ? Theme.onPaper
                                                                               : "transparent")
                        }

                        Glyph {
                            id: here

                            anchors.left: parent.left
                            anchors.leftMargin: Widths.scale.size(6)
                            anchors.verticalCenter: parent.verticalCenter
                            side: Widths.scale.icon(18)
                            source: "assets/icons/play_arrow.svg"
                            tint: Theme.emerald
                            visible: row.current
                        }

                        Text {
                            id: label

                            objectName: "reader-volume-chapter-" + row.index + "-text"
                            anchors.left: parent.left
                            anchors.leftMargin: Widths.scale.size(30)
                            anchors.right: where.left
                            anchors.rightMargin: Widths.scale.size(8)
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData
                                  ? ReaderCaptions.where("", row.modelData.label,
                                                         row.modelData.title)
                                  : ""
                            color: row.current ? Theme.emerald
                                               : (row.jumps ? Theme.ink : Theme.inkFaint)
                            font.family: Theme.textFamily
                            font.pixelSize: Widths.scale.body(14)
                            font.weight: row.current ? Font.Bold : Font.Normal
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                        }

                        Text {
                            id: where

                            objectName: "reader-volume-chapter-" + row.index + "-page"
                            anchors.right: parent.right
                            anchors.rightMargin: Widths.scale.size(8)
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData && row.jumps ? ReaderCaptions.chapterPage(row.modelData.startPage)
                                            : ""
                            color: Theme.inkFaint
                            font.family: Theme.textFamily
                            font.pixelSize: Widths.scale.caption(13)
                            textFormat: Text.PlainText
                        }

                        HoverHandler {
                            id: pointer

                            enabled: row.jumps
                            cursorShape: Qt.PointingHandCursor
                        }

                        TapHandler {
                            enabled: row.jumps
                            gesturePolicy: TapHandler.ReleaseWithinBounds
                            onTapped: Reader.goToPage(row.modelData.startPage)
                        }
                    }
                }
            }

            IsbnKey {
                objectName: "reader-volume-isbn-key"
                width: parent.width
                visible: card.barcoded
                code: card.barcode
            }
        }
    }
}
