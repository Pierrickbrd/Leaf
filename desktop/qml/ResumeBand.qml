// The one offer above the grid: where you were, and the word for going back to it.
//
// It is the grid's header rather than a sibling anchored above it, because the artifact says
// the band *leaves with the scroll* — the search field stays reachable three hundred series
// down, the band does not. A header scrolls with the content by construction; a sibling would
// need its own hide-on-scroll rule, which is a second opinion about the same movement.
//
// Absent, and zero pixels tall, when nothing is started: `/next` answers an empty list and the
// grid closes over the gap. Later, a running search will hide it the same way — the artifact
// says it goes while you are looking for something precise.
//
// Everything it displays is worded in C++ (`Resume`, `Words`). This file lays those strings
// out and paints them; it decides no French of its own.

import QtQuick
import Leaf

Item {
    id: band

    // Replaceable only so the QML can be exercised with a stand-in, exactly as ShelfView's
    // `sourceModel` is. The application never assigns it.
    property var sourceModel: Resume
    // A precise search removes the offer without replacing the model's own availability.
    property bool suppressed: false

    // Under 1100 px the long line no longer fits beside a button: the wording shortens, the
    // track halves, and the button keeps its arrow and drops its word. Read from Widths, which
    // is where the two breaks are decided, and never from a width compared here.
    // The one stop this band puts in the focus chain. ShelfView needs to recognise it there:
    // see the comment on `enterFromFocusChain`.
    readonly property Item button: action

    readonly property bool compact: Widths.band !== Widths.Band.Wide
    /// Under 700 high, one line: at 800 × 560 the band of 120 took 145 pixels of the 502 the
    /// shelf had, and left one row of covers cut in half (audit, 23/10/2026). A small cover,
    /// the name and « T12 · p. 48/190 » side by side, and the round button.
    readonly property bool oneLine: Widths.short
    /// The paper left of the card, above it, and the gap under it: the page's gutter, the
    /// page's top and one block, on the shelf, where the band is the first thing under the
    /// bar and must stand where every page's first block stands — the owner compared it with
    /// the search's tabs on 23/10/2026 and found them at 16 and 9. None of the three on the
    /// series page, whose column has its margins already — inset twice there, the card's
    /// edge matched neither the header above it nor the tabs below.
    property int inset: Widths.space.gutter
    property int above: Widths.space.top
    property int below: Widths.space.block
    /// What the screen around the band already says went wrong. A trouble that only repeats
    /// it is not drawn: with no address configured, the shelf's card and this band carried
    /// the same sentence one above the other in the test of 22/10/2026.
    property string echoed: ""
    readonly property bool troubled: sourceModel.trouble.length > 0
                                     && sourceModel.trouble !== echoed
    readonly property bool showing: !suppressed && (sourceModel.available || troubled)

    objectName: "resume-band"
    visible: showing
    // The page's top above the card; then the gap that separates it from what follows. A
    // band that keeps its height while invisible would leave that hole above a grid with
    // nothing in it.
    height: showing ? band.above + card.height + band.below : 0

    /// Asks for its model on creation, which is right for the one band that is built once
    /// and wrong for the one the Loader rebuilds. `Main.qml` says it of the page already —
    /// « a screen that fetches on creation would fetch again every time the Loader rebuilt
    /// it » — and this band was doing exactly that, one answer per series opened. The same
    /// switch `FilterPills` carries, for the same reason.
    property bool reloadOnCompleted: true

    Component.onCompleted: {
        if (reloadOnCompleted)
            sourceModel.reload()
    }

    Rectangle {
        id: card

        objectName: "resume-card"
        x: band.inset
        y: band.above
        // The page's whole width inside its gutters, as the grid under it. Bounded to
        // column(760) on 23/10/2026 so that the button stood nearer the name, it read at 1920
        // as a card that had lost half its width and stopped short of the edge the covers
        // keep on the right (owner, 25/10/2026).
        width: Math.max(0, band.width - 2 * band.inset)
        // The tallest of what it holds, and not the cover alone: words that follow the
        // desktop's text size can outgrow a cover of 93, and a card held to it cut them.
        height: (band.sourceModel.available
                 ? Math.max(cover.height, said.implicitHeight, action.height)
                 : Math.max(cover.height, troubleText.implicitHeight)) + 2 * padding
        radius: Theme.cardRadius
        color: Theme.surface

        // The artifact's 0.8rem at this scale.
        readonly property int padding: Widths.scale.size(band.oneLine ? 7 : 14)

        // The same measured card elevation the covers carry, from the same nine-slice: its
        // middle is the part that stretches, so one texture serves a 2:3 cover and a band.
        CoverShadow {
            under: "resume"
            z: -1
        }

        Accessible.role: Accessible.StaticText
        Accessible.name: band.sourceModel.seriesName
        Accessible.description: band.sourceModel.where

        Item {
            id: cover

            objectName: "resume-cover-frame"
            x: card.padding
            y: card.padding
            // 2:3, like every cover in the client. A cover is the one thing on this card worth
            // recognising from across the room, so the width goes here rather than to the text.
            // Two and a half times the name written beside it, which is the proportion the
            // design draws. At four times it was a poster with a caption, and the band is a
            // line about where one stopped — not a second header.
            width: Widths.scale.size(band.oneLine ? 28 : 62)
            height: Math.round(width * 1.5)
            visible: band.sourceModel.available

            CoverShadow {
                under: "resume-cover"
            }

            RoundedCover {
                objectName: "resume-cover"
                anchors.fill: parent
                source: band.sourceModel.cover
            }

            // The hairline every other cover in this client wears. Written out here, it was
            // the dark line without the pale catch of light along the top — the same object
            // as the others, one detail short of looking like them.
            CoverSkin { }
        }

        // One column, centred on the card: the artifact centres it, and a card whose three
        // lines hang from the top leaves a hole under the track. Column skips a hidden child,
        // so a next-up card with no progress centres its two lines by itself.
        Column {
            id: said

            objectName: "resume-said"
            anchors.left: cover.right
            anchors.leftMargin: Widths.scale.room(14)
            anchors.right: action.left
            anchors.rightMargin: Widths.scale.room(14)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Widths.scale.room(4)
            visible: band.sourceModel.available

            // The name, and on a short window where one is beside it on the same line.
            Item {
                width: parent.width
                height: seriesName.implicitHeight

                Text {
                    id: seriesName

                    objectName: "resume-name"
                    width: band.oneLine
                           ? Math.max(0, Math.min(implicitWidth, parent.width
                                                  - besideName.implicitWidth
                                                  - Widths.scale.room(10)))
                           : parent.width
                    text: band.sourceModel.seriesName
                    // The series' name and the chapter's are the files' words: a title such as
                    // « <b>x</b> » is shown as written, never drawn as markup.
                    textFormat: Text.PlainText
                    color: Theme.ink
                    font.family: Theme.displayFamily
                    font.pixelSize: Widths.scale.title(band.oneLine ? 18 : 22)
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }

                Text {
                    id: besideName

                    objectName: "resume-where-beside"
                    x: seriesName.width + Widths.scale.room(10)
                    width: Math.max(0, parent.width - x)
                    anchors.baseline: seriesName.baseline
                    text: band.sourceModel.whereShort
                    textFormat: Text.PlainText
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(15)
                    elide: Text.ElideRight
                    visible: band.oneLine
                }
            }

            Text {
                objectName: "resume-where"
                width: parent.width
                text: band.compact ? band.sourceModel.whereShort : band.sourceModel.where
                textFormat: Text.PlainText
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.body(15)
                elide: Text.ElideRight
                visible: !band.oneLine
            }

            // The track hangs a little further below the line than the lines do from each
            // other, so the three do not read as one paragraph.
            Item {
                width: 1
                height: Widths.scale.room(6)
                visible: track.visible
            }

            Rectangle {
                id: track

                objectName: "resume-progress"
                // The artifact's 11 rem, and 7 at half a screen, against a 2.9 rem cover.
                width: Math.min(Widths.scale.size(band.compact ? 150 : 240),
                                Math.max(0, said.width))
                height: Widths.scale.size(4)
                radius: height / 2
                color: Theme.onBar
                // Nothing started is not a bar at zero: `reason` is NEXT_UP and there is no
                // progress to draw at all. Nor on one line, where « p. 48/190 » says it.
                visible: band.sourceModel.hasProgress && !band.oneLine

                Rectangle {
                    objectName: "resume-progress-fill"
                    width: Math.round(parent.width * band.sourceModel.progress)
                    height: parent.height
                    radius: parent.radius
                    color: Theme.emerald
                }
            }
        }

        ActionButton {
            id: action

            objectName: "resume-action"
            anchors.right: parent.right
            anchors.rightMargin: card.padding
            anchors.verticalCenter: parent.verticalCenter
            label: band.sourceModel.action
            compact: band.compact || band.oneLine
            // Its word follows the window as the band's do; at its drawn 14 it stood beside a
            // name that had shrunk to 19.
            scaled: true
            visible: band.sourceModel.available
            // Out of Qt's own Tab traversal, because that traversal cannot express the order
            // this screen reads in — bar, band, chips, covers — and it consumes the key at
            // the focused item before any screen gets to say so. The screen drives it, and
            // this stays focusable by being given the focus rather than by being traversed.
            activeFocusOnTab: false
            // In the keyboard chain with the covers below, deliberately before the reader it
            // will open exists: a control put into the workflow "later" is one nobody puts in.
            Accessible.description: band.sourceModel.seriesName
            // Where one was, opened: the volume, at the page the band is offering.
            onTriggered: Navigation.open(Navigation.Reader, {
                "series": band.sourceModel.seriesId,
                "entry": band.sourceModel.entryId
            })

            // The arrow, drawn rather than typed — see ActionButton's header for why the glyph
            // comes from the caller.
            Canvas {
                id: arrow

                objectName: "resume-arrow"
                width: Widths.scale.icon(10)
                height: Widths.scale.icon(12)
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                onPaint: {
                    const ink = getContext("2d")
                    ink.reset()
                    ink.fillStyle = Theme.onEmerald
                    ink.beginPath()
                    ink.moveTo(0, 0)
                    ink.lineTo(width, height / 2)
                    ink.lineTo(0, height)
                    ink.closePath()
                    ink.fill()
                }

                Connections {
                    target: Theme

                    function onChanged() {
                        arrow.requestPaint()
                    }
                }
            }
        }

        // What went wrong instead of the offer, in the card that would have carried it. The
        // shelf has a trouble box of its own; a band that failed silently would be read as a
        // library with nothing started in it.
        Text {
            id: troubleText

            objectName: "resume-trouble"
            anchors.fill: parent
            anchors.margins: card.padding
            textFormat: Text.PlainText
            text: band.sourceModel.trouble
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(15)
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignVCenter
            visible: !band.sourceModel.available && band.troubled
        }
    }
}
