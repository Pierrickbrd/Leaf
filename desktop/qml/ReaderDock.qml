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
// **A webtoon's card is another row** (maquette Figma « Bande du bas · lecteur webtoon »,
// validated on 24/11/2026). A strip has no fixed page, so the scrub and « 1 / 29 » said nothing
// of where the reader was: the card names the chapter among the others and steps from one to
// the next, carries the zoom — − 100 % + — and the A that starts the strip going by itself,
// green while it does. Where in the chapter is the thin mark on the right of the screen
// (`ReaderStripMark`), and the speed a setting of the reader's card, not of this one.
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
    /// A webtoon's row: the strip's zoom against its 100 %, whether its A runs, and whether a
    /// pinch's glass is over it — said in the chapter's place, which is where the eye goes.
    property real stripLevel: 1
    property bool autoReading: false
    property bool glass: false

    /// A page step pressed: the view turns it, so that it slides as any turn does.
    signal forwardRequested()
    signal backwardRequested()
    /// A webtoon's zoom step, 1 larger and −1 smaller, and its A pressed.
    signal zoomStepped(int direction)
    signal autoReadToggled()

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

        // A stop, as the bar's buttons are: the chapter steps had no key at all.
        activeFocusOnTab: true
        Accessible.focusable: true

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

    /// The thin upright line between groups.
    component Rule: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: Widths.scale.size(20)
        Layout.leftMargin: Widths.scale.room(4)
        Layout.rightMargin: Widths.scale.room(4)
        color: Theme.rule
    }

    RowLayout {
        objectName: "reader-book-row"
        anchors.fill: parent
        anchors.leftMargin: Widths.scale.room(10)
        anchors.rightMargin: Widths.scale.room(14)
        spacing: Widths.scale.room(4)
        visible: !Reader.webtoon

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

        Rule {}

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
                textFormat: Text.PlainText
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
                    textFormat: Text.PlainText
                    text: Reader.firstPage >= 0 ? String(Reader.firstPage + 1) : ""
                    color: Theme.ink
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(13)
                    font.weight: Font.Medium
                }

                Text {
                    objectName: "reader-page-count"
                    textFormat: Text.PlainText
                    text: ReaderCaptions.outOf(Reader.pageCount)
                    color: Theme.inkFaint
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(13)
                }
            }
        }
    }

    RowLayout {
        objectName: "reader-strip-row"
        anchors.fill: parent
        anchors.leftMargin: Widths.scale.room(10)
        anchors.rightMargin: Widths.scale.room(10)
        spacing: Widths.scale.room(4)
        visible: Reader.webtoon

        Step {
            objectName: "reader-entry-backward"
            glyph: "keyboard_double_arrow_left"
            label: ReaderCaptions.previousChapter
            // There, and faint, when nothing comes before: the card does not jump about.
            opacity: Reader.hasPrevious ? 1 : 0.35
            onTriggered: Reader.openPreviousEntry()
        }

        Column {
            Layout.fillWidth: true
            Layout.leftMargin: Widths.scale.room(6)
            spacing: Widths.scale.room(1)

            Text {
                objectName: "reader-entry-name"
                width: parent.width
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: dock.glass ? ReaderCaptions.loupe : Reader.entryLabel
                color: Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(13)
                font.weight: Font.Medium
            }

            Text {
                objectName: "reader-entry-among"
                visible: !dock.glass && Reader.entryCount > 0
                textFormat: Text.PlainText
                text: ReaderCaptions.among(Reader.entryCount)
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(11)
            }
        }

        Step {
            objectName: "reader-entry-forward"
            glyph: "keyboard_double_arrow_right"
            label: ReaderCaptions.nextChapter
            opacity: Reader.next.entryId !== undefined ? 1 : 0.35
            onTriggered: Reader.openNextEntry()
        }

        Rule {}

        Step {
            objectName: "reader-zoom-out"
            glyph: "remove"
            label: ReaderCaptions.zoomOut
            onTriggered: dock.zoomStepped(-1)
        }

        // As wide as « 400 % », aligned on its right: the + does not move as the level does.
        Text {
            objectName: "reader-strip-level"
            Layout.preferredWidth: Math.ceil(widestLevel.advanceWidth)
            horizontalAlignment: Text.AlignRight
            textFormat: Text.PlainText
            text: ReaderCaptions.zoom(dock.stripLevel)
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(13)
            font.weight: Font.Medium

            TextMetrics {
                id: widestLevel

                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(13)
                font.weight: Font.Medium
                text: ReaderCaptions.zoom(4)
            }
        }

        Step {
            objectName: "reader-zoom-in"
            glyph: "add"
            label: ReaderCaptions.zoomIn
            onTriggered: dock.zoomStepped(1)
        }

        Rule {}

        // The A: a letter and not a picto, as on the mockup, on the theme's green while the
        // strip goes by itself.
        Rectangle {
            id: auto

            readonly property bool hovered: autoHover.hovered
            readonly property string label: ReaderCaptions.autoRead(dock.autoReading)

            objectName: "reader-auto-read"
            Layout.preferredWidth: Widths.scale.size(26)
            Layout.preferredHeight: Widths.scale.size(26)
            radius: height / 2
            color: dock.autoReading ? Theme.emerald : auto.hovered ? Theme.onPaper : "transparent"
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: auto.label
            Accessible.focusable: true
            Accessible.onPressAction: dock.autoReadToggled()

            Keys.onPressed: event => {
                if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                        || event.key === Qt.Key_Enter) {
                    if (!event.isAutoRepeat)
                        dock.autoReadToggled()
                    event.accepted = true
                }
            }

            FocusRing {
                cornerRadius: auto.radius
            }

            HoverHandler {
                id: autoHover

                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: dock.autoReadToggled()
            }

            Text {
                anchors.centerIn: parent
                textFormat: Text.PlainText
                text: "A"
                color: dock.autoReading ? Theme.onEmerald : Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(13)
                font.weight: Font.DemiBold
            }

            LeafToolTip {
                visible: auto.hovered
                text: auto.label
            }
        }
    }
}
