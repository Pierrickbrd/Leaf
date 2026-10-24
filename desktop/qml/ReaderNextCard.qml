// What the reader offers when the last page has been turned.
//
// A book that simply stopped sent the reader back to the series page to find the next volume
// themselves, which is the one moment they are most likely to want it (spec §07). So the last
// page stays, veiled and not hidden, and a card floats at the middle of it: what was finished,
// the volume that follows with its cover, one filled button to read it, and two quiet ways
// out. When nothing follows, the card says so and keeps only the way back.
//
// **It shows `Reader`'s state and decides nothing.** `Reader.atTheEnd` raises it, `Reader.next`
// and `Reader.ending` word it, and the three outcomes leave as signals for the view to carry
// out: the view already owns the turns, Escape and `Navigation`, and a second place that knew
// what « forward » means here is how a button and a key come to disagree.
//
// **The cover may never arrive.** The server can refuse it, or not have one for a volume that
// is a file and nothing else; `RoundedCover` draws its paper under a picture that did not
// load, so the card reads as a title with a blank beside it and not as a hole.
//
// **It follows the window as the dock does** (`Widths.scale`): every size below, words and
// cover included, is drawn for a window of 1280 and scaled with it, its words never under
// their floor, and the card is never wider nor taller than the window leaves. In the
// test of 22/10/2026 on a full-screen window of 1920, the dock had grown and this card had
// kept its drawn size beside it.
//
// **Titles are server data and are drawn as text.** A ComicInfo can carry `<b>` or a bare
// `&`, and the default `AutoText` would read them as markup (see SeriesDescription).

import QtQuick
import Leaf

Item {
    id: end

    /// The button, or a turn forward, which is the same thing.
    signal readNextRequested()
    signal backToSeriesRequested()
    /// « Rester sur cette page », and Escape.
    signal stayRequested()

    readonly property bool followed: Reader.next.entryId !== undefined
    readonly property int padding: Widths.scale.size(22)

    /// Whether a point of the view is on the card itself and not on the veil round it.
    function holds(x, y) {
        return x >= face.x && x < face.x + face.width && y >= face.y && y < face.y + face.height
    }

    objectName: "reader-end"
    visible: Reader.atTheEnd

    // The page is veiled, not hidden: the reader finished it a moment ago and may want to
    // look at it again. Darker in the dark theme, where the paper is already dim.
    Rectangle {
        anchors.fill: parent
        color: Theme.dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(0.047, 0.063, 0.055, 0.42)
    }

    Rectangle {
        id: face

        objectName: "reader-end-card"
        x: Math.round((end.width - face.width) / 2)
        y: Math.round((end.height - face.height) / 2)
        width: Math.min(end.width - 32, Widths.scale.column(380))
        // Never taller than the window less a margin: on a short window, with a long title
        // wrapped over three lines, what it holds scrolls inside it rather than off the screen.
        height: Math.min(column.height + 2 * end.padding, end.height - 32)
        radius: Widths.scale.size(Theme.cardRadius)
        color: Theme.surface
        border.color: Theme.rule
        border.width: 1
        antialiasing: true

        CardLift { level: CardLift.Modal }

        Flickable {
            id: sheet

            objectName: "reader-end-sheet"
            y: end.padding
            width: face.width
            height: face.height - 2 * end.padding
            contentHeight: column.height
            clip: true
            interactive: column.height > sheet.height
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: column

                x: end.padding
                width: face.width - 2 * end.padding
                spacing: Widths.scale.size(16)

                Row {
                    width: parent.width
                    spacing: Widths.scale.size(7)

                    Glyph {
                        id: tick

                        anchors.top: parent.top
                        anchors.topMargin: 1
                        side: Widths.scale.icon(18)
                        source: "assets/icons/check_circle.svg"
                        tint: Theme.emerald
                    }

                    Text {
                        objectName: "reader-end-done"
                        width: parent.width - tick.width - parent.spacing
                        text: Reader.ending
                        color: Theme.emerald
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(14)
                        font.weight: Font.Medium
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                    }
                }

                Row {
                    objectName: "reader-end-what"
                    width: parent.width
                    spacing: Widths.scale.size(14)
                    visible: end.followed

                    Item {
                        id: cover

                        width: Widths.scale.size(68)
                        height: Widths.scale.size(102)

                        // Only asked for while the card is up: `Reader.ahead` has already had
                        // it fetched three pages from the end, so this finds it in the cache.
                        RoundedCover {
                            objectName: "reader-end-cover"
                            anchors.fill: parent
                            radius: Widths.scale.size(Theme.coverRadius)
                            source: end.visible && end.followed ? Reader.next.cover : ""
                        }

                        CoverSkin {
                            radius: Widths.scale.size(Theme.coverRadius)
                        }
                    }

                    Text {
                        objectName: "reader-end-title"
                        width: parent.width - cover.width - parent.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: end.followed ? Reader.next.label : ""
                        color: Theme.ink
                        font.family: Theme.displayFamily
                        font.pixelSize: Widths.scale.title(20)
                        font.weight: Font.Bold
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                    }
                }

                ActionButton {
                    objectName: "reader-end-go"
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: end.followed
                    label: end.followed ? Reader.next.action : ""
                    scaled: true
                    onTriggered: end.readNextRequested()

                    Glyph {
                        side: Widths.scale.icon(18)
                        source: "assets/icons/play_arrow.svg"
                        tint: Theme.onEmerald
                    }
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Widths.scale.size(8)

                    LeafTextAction {
                        objectName: "reader-end-back"
                        label: ReaderCaptions.backToSeries
                        quiet: true
                        scaled: true
                        onTriggered: end.backToSeriesRequested()
                    }

                    LeafTextAction {
                        objectName: "reader-end-stay"
                        label: ReaderCaptions.stayHere
                        quiet: true
                        scaled: true
                        // Only the last volume loses it (spec 07): while the way on is out, or
                        // failed, nothing says that nothing follows.
                        visible: !Reader.lastOfSeries
                        onTriggered: end.stayRequested()
                    }
                }
            }
        }
    }
}
