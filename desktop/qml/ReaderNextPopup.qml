// What a webtoon offers at the foot of a chapter: the next one, for six seconds.
//
// A strip does not run on into the next chapter — two chapters are two files, each with its
// own place kept — and the book's end card, a veil and a card over the whole screen, was
// heavier than a strip read in one long scroll asks for (decision of 24/11/2026). So the foot
// of a chapter brings a small card at the middle of the screen, fading in, with the chapter
// that follows and one button to read it, and it goes by itself after six seconds. The next
// chapter's first pages are already fetched by then (`Reader.ahead`): it opens on pages that
// are there. With nothing after, it is the book's end card that comes, with its way back.
//
// **It shows `Reader.next` and decides nothing**: the button is a signal for the view.

import QtQuick
import Leaf

Item {
    id: offer

    /// Up: the view says when, and puts it away.
    property bool shown: false
    readonly property bool followed: Reader.next.entryId !== undefined

    signal readNextRequested()

    objectName: "reader-next-offer"
    width: face.width
    height: face.height
    opacity: offer.shown && offer.followed ? 1 : 0
    visible: opacity > 0

    Behavior on opacity {
        enabled: Preferences.motion !== Preferences.Instant

        NumberAnimation {
            duration: 260
            easing.type: Easing.OutCubic
        }
    }

    Rectangle {
        id: face

        readonly property int padding: Widths.space.card

        width: row.width + 2 * face.padding
        height: row.height + 2 * face.padding
        radius: Widths.scale.size(18)
        color: Theme.surface

        CardLift {
            level: CardLift.Bubble
        }

        Row {
            id: row

            x: face.padding
            y: face.padding
            spacing: Widths.scale.size(14)

            Item {
                width: Widths.scale.size(46)
                height: Widths.scale.size(68)

                RoundedCover {
                    objectName: "reader-next-offer-cover"
                    anchors.fill: parent
                    radius: Widths.scale.size(Theme.coverRadius)
                    source: offer.visible && offer.followed ? Reader.next.cover : ""
                }

                CoverSkin {
                    radius: Widths.scale.size(Theme.coverRadius)
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Widths.scale.size(8)

                Text {
                    objectName: "reader-next-offer-title"
                    textFormat: Text.PlainText
                    text: offer.followed ? Reader.next.label : ""
                    color: Theme.ink
                    font.family: Theme.displayFamily
                    font.pixelSize: Widths.scale.title(18)
                    font.weight: Font.Bold
                }

                ActionButton {
                    objectName: "reader-next-offer-go"
                    label: offer.followed ? Reader.next.action : ""
                    scaled: true
                    onTriggered: offer.readNextRequested()

                    Glyph {
                        side: Widths.scale.icon(18)
                        source: "assets/icons/play_arrow.svg"
                        tint: Theme.onEmerald
                    }
                }
            }
        }
    }
}
