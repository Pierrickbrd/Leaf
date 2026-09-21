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
import Qt5Compat.GraphicalEffects
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
    readonly property bool showing: !suppressed
                                    && (sourceModel.available || sourceModel.trouble.length > 0)

    objectName: "resume-band"
    visible: showing
    // The same 16 px the covers keep from the left edge, above the card; then the gap that
    // separates it from the first row. A band that keeps its height while invisible would
    // leave that hole above a grid with nothing in it.
    height: showing ? Widths.shelfMargin + card.height + Widths.shelfGap : 0

    Component.onCompleted: sourceModel.reload()

    Rectangle {
        id: card

        objectName: "resume-card"
        x: Widths.shelfMargin
        y: Widths.shelfMargin
        width: Math.max(0, band.width - 2 * Widths.shelfMargin)
        // The cover sets the height, and the padding is the artifact's 0.8rem at this scale.
        height: cover.height + 2 * padding
        radius: Theme.cardRadius
        color: Theme.surface

        readonly property int padding: 14

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
            width: 80
            height: 120
            visible: band.sourceModel.available

            Image {
                id: art

                objectName: "resume-cover"
                anchors.fill: parent
                source: band.sourceModel.cover
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
                layer.enabled: true
                layer.effect: OpacityMask {
                    maskSource: Rectangle {
                        width: cover.width
                        height: cover.height
                        radius: Theme.coverRadius
                        antialiasing: true
                    }
                }
            }

            // The hairline the covers carry in the grid: without it a pale cover has no edge
            // against the card it sits on.
            Rectangle {
                anchors.fill: parent
                radius: Theme.coverRadius
                color: "transparent"
                border.color: Theme.rule
                border.width: 1
                antialiasing: true
            }
        }

        // One column, centred on the card: the artifact centres it, and a card whose three
        // lines hang from the top leaves a hole under the track. Column skips a hidden child,
        // so a next-up card with no progress centres its two lines by itself.
        Column {
            id: said

            objectName: "resume-said"
            anchors.left: cover.right
            anchors.leftMargin: 14
            anchors.right: action.left
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            visible: band.sourceModel.available

            Text {
                id: seriesName

                objectName: "resume-name"
                width: parent.width
                text: band.sourceModel.seriesName
                color: Theme.ink
                font.family: Theme.displayFamily
                font.pixelSize: 20
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            Text {
                objectName: "resume-where"
                width: parent.width
                text: band.compact ? band.sourceModel.whereShort : band.sourceModel.where
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: 14
                elide: Text.ElideRight
            }

            // The track hangs a little further below the line than the lines do from each
            // other, so the three do not read as one paragraph.
            Item {
                width: 1
                height: 6
                visible: band.sourceModel.hasProgress
            }

            Rectangle {
                id: track

                objectName: "resume-progress"
                // The artifact's 11 rem, and 7 at half a screen, against a 2.9 rem cover.
                width: Math.min(band.compact ? 150 : 240, Math.max(0, said.width))
                height: 4
                radius: height / 2
                color: Theme.onBar
                // Nothing started is not a bar at zero: `reason` is NEXT_UP and there is no
                // progress to draw at all.
                visible: band.sourceModel.hasProgress

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
            compact: band.compact
            visible: band.sourceModel.available
            // Out of Qt's own Tab traversal, because that traversal cannot express the order
            // this screen reads in — bar, band, chips, covers — and it consumes the key at
            // the focused item before any screen gets to say so. The screen drives it, and
            // this stays focusable by being given the focus rather than by being traversed.
            activeFocusOnTab: false
            // In the keyboard chain with the covers below, deliberately before the reader it
            // will open exists: a control put into the workflow "later" is one nobody puts in.
            Accessible.description: band.sourceModel.seriesName

            // The arrow, drawn rather than typed — see ActionButton's header for why the glyph
            // comes from the caller.
            Canvas {
                id: arrow

                objectName: "resume-arrow"
                width: 10
                height: 12

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
            objectName: "resume-trouble"
            anchors.fill: parent
            anchors.margins: card.padding
            text: band.sourceModel.trouble
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 15
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignVCenter
            visible: !band.sourceModel.available && band.sourceModel.trouble.length > 0
        }
    }
}
