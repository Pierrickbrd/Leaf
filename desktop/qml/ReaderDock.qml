// The card at the foot of the page: where in the volume, and the steps to move through it.
//
// A card and not a band: it floats clear of the edges on the blurred lift every card in the
// application has, so the page stays visible all round it rather than losing a strip of the
// screen's width (spec §02). One point six per cent of the height above the edge and a radius
// of eighteen, both the spec's.
//
// **Its buttons are placed the way the book reads.** From the outside in, a chapter then a
// page, and in a manga the ones on the left go forward — like the left third of the screen,
// so a reader who learned one side has learned both. The row is mirrored rather than its
// buttons renamed: what a button does stays its name, and only where it is drawn changes.
//
// **It follows the window** (`Widths.scale`): every size below is drawn for a window of 1280
// and scaled with it, larger past it and smaller under it, its words never under their floor
// and never over their drawn size, its glyphs at half the rate and its gaps at twice it — on
// 23/10/2026, at 1920, the words had grown with it and the room between them had not.
// Tried by hand on 22/10/2026 in full screen on a window of 1920, the card was a strip of
// specks at the foot of the page at its drawn size, and too large for it at half as large
// again. In the narrow band it drops its chapter steps rather than shrink further.
//
// A step taken here leaves the card up. The card was called to move through the book; sending
// it away at the first page turned would make a reader call it again for the second.

import QtQuick
import QtQuick.Layouts
import Leaf

Rectangle {
    id: dock

    property bool rightToLeft: false
    /// « 200 % » while a zoom is being said, empty the rest of the time: it takes the place
    /// of the page number for that second (spec §09).
    property string level: ""
    /// Whether any chapter says where it begins. Without one the chapter steps have nowhere
    /// to go, and two buttons that do nothing are two buttons that look broken.
    readonly property bool marked: {
        for (const one of Reader.chapters) {
            if (one.startPage >= 0)
                return true
        }
        return false
    }

    /// The chapter steps, when there are chapters to step to and room for them. In the narrow
    /// band they go: four buttons and the bar between them, scaled down to fit, were specks
    /// round a bar too short to aim at, and a chapter is still a click away in the volume's
    /// card.
    readonly property bool shown: dock.marked && Widths.band !== Widths.Band.Narrow

    /// A page step pressed: the view turns it, so that it slides as any turn does.
    signal forwardRequested()
    signal backwardRequested()

    /// The round button of the card, with the bubble saying what it does.
    component Step: IconButton {
        id: step

        // The size it is drawn at, read from the scale and never back from `width`: a row
        // writes the width it lays a child out at, and a preferred width taken from that
        // width keeps whatever the row first wrote. Qt 6.4 (Ubuntu 24.04) wrote 24 and the
        // page step stayed there at every window size.
        readonly property real drawn: Widths.scale.size(26)

        // The layout's size and not only the item's: a row lays a child out at what it was
        // told it prefers, and a width alone was taken once and never again. Not squeezed
        // either.
        width: step.drawn
        height: step.drawn
        Layout.preferredWidth: step.drawn
        Layout.preferredHeight: step.drawn
        Layout.minimumWidth: step.drawn
        Layout.minimumHeight: step.drawn
        side: Widths.scale.icon(20)

        LeafToolTip {
            visible: step.hovered
            text: step.label
        }
    }

    objectName: "reader-dock"
    width: Math.min(Widths.scale.size(640), (parent ? parent.width : 640) * 0.78)
    // Fifty, the spec's card, and taller by what the line under the progress bar needs.
    height: Math.max(Widths.scale.size(50), progress.implicitHeight + Widths.scale.size(16))
    radius: Widths.scale.size(18)
    color: Theme.surface

    CardLift {
        level: CardLift.Bubble
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Widths.scale.room(10)
        anchors.rightMargin: Widths.scale.room(14)
        spacing: Widths.scale.room(4)

        RowLayout {
            Layout.fillWidth: true
            spacing: Widths.scale.room(4)
            // Written in reading order, laid out in the book's: in a manga the first of these
            // is drawn on the right.
            layoutDirection: dock.rightToLeft ? Qt.RightToLeft : Qt.LeftToRight

            Step {
                objectName: "reader-chapter-backward"
                visible: dock.shown
                glyph: dock.rightToLeft ? "keyboard_double_arrow_right"
                                        : "keyboard_double_arrow_left"
                label: ReaderCaptions.previousChapter
                onTriggered: Reader.previousChapter()
            }

            Step {
                objectName: "reader-page-backward"
                glyph: dock.rightToLeft ? "chevron_right" : "chevron_left"
                label: ReaderCaptions.previousPage
                onTriggered: dock.backwardRequested()
            }

            ReaderScrub {
                id: progress

                Layout.fillWidth: true
                Layout.leftMargin: Widths.scale.room(6)
                Layout.rightMargin: Widths.scale.room(6)
                rightToLeft: dock.rightToLeft
            }

            Step {
                objectName: "reader-page-forward"
                glyph: dock.rightToLeft ? "chevron_left" : "chevron_right"
                label: ReaderCaptions.nextPage
                onTriggered: dock.forwardRequested()
            }

            Step {
                objectName: "reader-chapter-forward"
                visible: dock.shown
                glyph: dock.rightToLeft ? "keyboard_double_arrow_left"
                                        : "keyboard_double_arrow_right"
                label: ReaderCaptions.nextChapter
                onTriggered: Reader.nextChapter()
            }
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: Widths.scale.size(20)
            Layout.leftMargin: Widths.scale.room(4)
            Layout.rightMargin: Widths.scale.room(4)
            color: Theme.rule
        }

        // The level in the number's place, for its second, in a box as wide as whichever is
        // wider: the steps on the left of the card stay where the hand left them.
        Item {
            implicitWidth: Math.max(where.implicitWidth, said.implicitWidth)
            implicitHeight: where.implicitHeight

            Text {
                id: said

                objectName: "reader-zoom-level"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: dock.level.length > 0
                text: dock.level
                color: Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(13)
                font.weight: Font.Medium
            }

            // « 47 / 190 », the number in a box as wide as the widest it can be. Tabular figures
            // are what the spec asks for so it does not dance while pages turn, and Qt 6.4 — the
            // one the CI builds with — cannot ask a font for them; a fixed width, aligned on its
            // right, holds the slash still instead.
            Row {
                id: where

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: dock.level.length === 0
                spacing: Widths.scale.room(4)

                TextMetrics {
                    id: widest

                    font: number.font
                    text: String(Reader.pageCount).replace(/[0-9]/g, "0")
                }

                Text {
                    id: number

                    objectName: "reader-page-number"
                    width: Math.ceil(widest.advanceWidth)
                    horizontalAlignment: Text.AlignRight
                    text: Reader.firstPage >= 0 ? String(Reader.firstPage + 1) : ""
                    color: Theme.ink
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(13)
                    font.weight: Font.Medium
                }

                Text {
                    objectName: "reader-page-count"
                    text: ReaderCaptions.outOf(Reader.pageCount)
                    color: Theme.inkFaint
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(13)
                }
            }
        }
    }
}
