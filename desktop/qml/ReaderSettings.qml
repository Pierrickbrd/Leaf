// The reader's settings, on a card that opens under `tune`.
//
// Two groups, named for how far each reaches. « Pour cette série » is kept on the server and
// follows the series to every device; « Sur cet appareil » is kept on this desk and applies to
// every book. They are on one card, and not in the Settings screen, because the moment to
// choose either is the moment a page is wrong in front of you — a manga read the wrong way
// round — and sending the reader out of the book to fix it is sending them away from the page
// they were looking at.
//
// **A pill is lit by what the server kept, not by what was clicked.** The direction group
// reads `Reader.chosenDirection`, which changes when the answer arrives; a click that was
// refused changes nothing, so the pill that was lit stays lit and the refusal is said in a
// bubble. A pill lit on the click and put back on the answer would flicker a lie for as long
// as the server took.
//
// **A webtoon keeps the fit and the backdrop, and nothing else.** It has no sense of reading,
// no pair of pages to lay out and no page to turn, and « Hauteur » means nothing for a column
// with no end. A fit chosen for books as « Hauteur » is shown as « Largeur », which is what a
// strip does with it. The paper behind a strip is still the reader's paper.
//
// **The backdrop and the motion are the owner's choice of 22/10/2026**: a black paper in a lit
// room tires some eyes, and a slide at every page is too much for some readers. Both are kept
// on this desk, with the layout.
//
// **Each row of « Sur cet appareil » says what it chooses**, beside its pills, under one of
// two small headings — « Pages » and « Confort ». The owner's critique of 22/10/2026 was four
// anonymous rows of pills: nothing said which row was the layout, the fit, the paper or the
// motion, and a reader learnt it by clicking. The pictos left when the names came: a row
// named « Fond » needs no moon beside « Noir », and its pills show the paper itself instead.
// The names stand in a column of their own so the pills of every row begin at one edge, as
// the approved mockup draws them; on a card too narrow for a name and its pills side by side,
// the name goes above its pills rather than squeezing them onto a second line beside it.
//
// **It follows the window as the dock does** (`Widths.scale`): every size below is drawn for a
// window of 1280 and scaled with it, its words never under their floor. In the owner's test of
// 22/10/2026 on a full-screen window of 1920 × 1200, the dock had grown and this card, beside
// it, had kept its drawn size — words too small to read at arm's length. On a short window its
// groups scroll under its title.
//
// Every word is `ReaderCaptions`'; this lays them out.

import QtQuick
import Leaf

Rectangle {
    id: card

    readonly property int padding: Widths.scale.size(18)
    /// The most height the window leaves it. Taller than that, the card stops at it and its
    /// groups scroll inside: on a short window it ran off the foot of the screen.
    property real room: Number.POSITIVE_INFINITY
    /// The column the rows' names stand in, and what is left between it and the pills.
    readonly property int labelColumn: Widths.scale.column(130)
    readonly property int labelGap: Widths.scale.size(14)
    /// A name above its pills rather than beside them: when the widest row of pills would not
    /// fit beside the column of names. Measured from the pills on one line, which do not
    /// depend on the card's width, so the choice never feeds back into what it is made from.
    readonly property bool stacked: body.width < card.labelColumn + card.labelGap
                                    + Math.max(layoutChoice.lineWidth, fitChoice.lineWidth,
                                               backdropChoice.lineWidth, motionChoice.lineWidth)
    /// The fit a strip actually draws with, so that the pill lit is the one that is true.
    readonly property int stripFit: Preferences.fit === Preferences.Original
                                    ? Preferences.Original : Preferences.Width

    /// The cross was pressed: the card asks to be closed, as Escape does.
    signal closeRequested()

    objectName: "reader-settings"
    height: Math.min(head.height + Widths.scale.size(10) + body.height + 2 * card.padding,
                     card.room)
    radius: Widths.scale.size(Theme.cardRadius)
    color: Theme.surface
    border.color: Theme.rule
    border.width: 1
    antialiasing: true

    CardLift { level: CardLift.Menu }

    component Heading: Text {
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        font.weight: Font.Medium
        font.capitalization: Font.AllUppercase
        font.letterSpacing: Widths.scale.letters(0.8)
    }

    /// « Pages », « Confort »: smaller and fainter than the heading over them, with a thin rule
    /// to the edge so each half reads as a band of its own.
    component SubHeading: Item {
        id: group

        property alias text: word.text

        width: parent.width
        height: word.height + Widths.scale.size(6)

        Text {
            id: word

            objectName: group.objectName + "-heading"
            anchors.bottom: parent.bottom
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(11)
            font.capitalization: Font.AllUppercase
            font.letterSpacing: Widths.scale.letters(1.2)
        }

        Rectangle {
            anchors.left: word.right
            anchors.leftMargin: Widths.scale.size(10)
            anchors.right: parent.right
            anchors.verticalCenter: word.verticalCenter
            height: 1
            color: Theme.rule
        }
    }

    /// One row: what it chooses, then its pills. The pills are the row's child, so each use
    /// keeps the `SettingsChoice` it always had, its name and its bindings.
    component DeskRow: Item {
        id: line

        required property string key
        property alias name: label.text
        property bool stacked: false
        property int labelColumn: 0
        property int labelGap: 0
        default property alias pills: slot.data

        objectName: key + "-row"
        width: parent.width
        height: stacked ? label.height + Widths.scale.size(6) + slot.height
                        : Math.max(label.height, slot.height)

        Text {
            id: label

            objectName: line.key + "-name"
            width: line.stacked ? line.width : line.labelColumn
            y: line.stacked ? 0 : (line.height - height) / 2
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
            wrapMode: Text.Wrap
        }

        Item {
            id: slot

            x: line.stacked ? 0 : line.labelColumn + line.labelGap
            y: line.stacked ? label.height + Widths.scale.size(6) : (line.height - height) / 2
            width: line.width - x
            height: childrenRect.height
        }
    }

    component Note: Text {
        width: parent.width
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.body(13)
        wrapMode: Text.Wrap
        lineHeight: 1.15
    }

    // The card's name and the way out of it, as the mockup draws them: a card with no
    // title says nothing of what it is for, and one with no cross is closed only by a
    // click the reader has to know to make.
    //
    // Above the groups and not among them: on a short window the groups scroll, and the
    // cross scrolled away with them would leave only Escape to close the card.
    Item {
        id: head

        x: card.padding
        y: card.padding
        width: card.width - 2 * card.padding
        height: Widths.scale.size(26)

        Text {
            objectName: "reader-settings-title"
            anchors.left: parent.left
            anchors.right: cross.left
            anchors.rightMargin: Widths.scale.size(8)
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: ReaderCaptions.cardTitle
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: Widths.scale.title(17)
            font.weight: Font.Bold
        }

        IconButton {
            id: cross

            objectName: "reader-settings-close"
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

        objectName: "reader-settings-sheet"
        x: 0
        y: head.y + head.height + Widths.scale.size(10)
        width: card.width
        height: card.height - sheet.y - card.padding
        contentHeight: body.height
        clip: true
        interactive: body.height > sheet.height
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: body

            x: card.padding
            width: card.width - 2 * card.padding
            spacing: Widths.scale.size(10)

            Heading {
                objectName: "reader-series-heading"
                visible: !Reader.webtoon
                text: ReaderCaptions.thisSeries
            }

            SettingsChoice {
                objectName: "reader-direction"
                visible: !Reader.webtoon
                width: parent.width
                scaled: true
                options: ReaderCaptions.directions
                chosen: Reader.chosenDirection
                onPicked: value => Reader.chooseDirection(value)
            }

            Note {
                objectName: "reader-declares"
                visible: !Reader.webtoon
                // The words are `Words`', bold part included: styled text is how the markup it
                // returns is drawn, and the only reason this is not plain.
                textFormat: Text.StyledText
                text: ReaderCaptions.declares(Reader.declaredDirection)
            }

            // The rule between what follows the series and what stays on this desk: two
            // reaches, and a line between them so neither is taken for part of the other.
            Item {
                visible: !Reader.webtoon
                width: parent.width
                height: Widths.scale.size(14)

                Rectangle {
                    objectName: "reader-settings-rule"
                    y: Math.round(parent.height / 2)
                    width: parent.width
                    height: 1
                    color: Theme.rule
                }
            }

            Heading {
                objectName: "reader-desk-heading"
                text: ReaderCaptions.thisDesk
            }

            SubHeading {
                objectName: "reader-pages"
                text: ReaderCaptions.pagesGroup
            }

            DeskRow {
                key: "reader-layout"
                name: ReaderCaptions.layoutName
                visible: !Reader.webtoon
                stacked: card.stacked
                labelColumn: card.labelColumn
                labelGap: card.labelGap

                SettingsChoice {
                    id: layoutChoice

                    objectName: "reader-layout"
                    width: parent.width
                    scaled: true
                    options: ReaderCaptions.layouts
                    chosen: Preferences.pageLayout
                    onPicked: value => Preferences.choosePageLayout(value)
                }
            }

            DeskRow {
                key: "reader-fit"
                name: ReaderCaptions.fitName
                stacked: card.stacked
                labelColumn: card.labelColumn
                labelGap: card.labelGap

                SettingsChoice {
                    id: fitChoice

                    objectName: "reader-fit"
                    width: parent.width
                    scaled: true
                    options: Reader.webtoon ? ReaderCaptions.stripFits : ReaderCaptions.fits
                    chosen: Reader.webtoon ? card.stripFit : Preferences.fit
                    onPicked: value => Preferences.chooseFit(value)
                }
            }

            SubHeading {
                objectName: "reader-comfort"
                text: ReaderCaptions.comfortGroup
            }

            DeskRow {
                key: "reader-backdrop"
                name: ReaderCaptions.backdropName
                stacked: card.stacked
                labelColumn: card.labelColumn
                labelGap: card.labelGap

                SettingsChoice {
                    id: backdropChoice

                    objectName: "reader-backdrop"
                    width: parent.width
                    scaled: true
                    options: ReaderCaptions.backdrops
                    // The very colours the pages will sit on, whichever theme is on.
                    swatches: ({
                        [Preferences.Black]: Theme.readerBlack,
                        [Preferences.Grey]: Theme.readerGrey,
                        [Preferences.White]: Theme.readerWhite
                    })
                    chosen: Preferences.backdrop
                    onPicked: value => Preferences.chooseBackdrop(value)
                }
            }

            DeskRow {
                key: "reader-motion"
                name: ReaderCaptions.motionName
                visible: !Reader.webtoon
                stacked: card.stacked
                labelColumn: card.labelColumn
                labelGap: card.labelGap

                SettingsChoice {
                    id: motionChoice

                    objectName: "reader-motion"
                    width: parent.width
                    scaled: true
                    options: ReaderCaptions.motions
                    chosen: Preferences.motion
                    onPicked: value => Preferences.chooseMotion(value)
                }
            }

            Note {
                objectName: "reader-desk-note"
                text: ReaderCaptions.deskNote
            }
        }
    }
}
