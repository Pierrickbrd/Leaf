// The reader's keys, on a card of their own, centred over the page under a light veil.
//
// Tried by hand on 22/10/2026, half the keys a reader reaches for did nothing, and the other
// half were nowhere written down: what the reader answers to was learned by pressing things.
// This card is the design approved that day — a keyboard in emerald and « Raccourcis », four
// groups in two columns (Naviguer and Affichage on the left, Zoom and Quitter on the right),
// each line its keys then what they do.
//
// **It is laid over the pages, never beside them.** The veil and the card come and go with a
// fade and a slight scale, and the book under them is not laid out again: a card about the
// keys is read and put away, and the page under it is still where it was.
//
// **It follows the window as the dock does** (`Widths.scale`): every size below — words, keys,
// glyphs, gaps — is drawn for a window of 1280 and scaled with it, its words never under their
// floor, and the card is never wider than nine tenths of the window. In the owner's test of
// 22/10/2026 on a full-screen window of 1920, the dock had grown and this card had kept its
// drawn size beside it.
//
// **The arrows are worded for the book open.** In a book read right to left ← goes on, as the
// left third of the screen does; the card said « Page précédente, suivante » over ← → there,
// the opposite of what the keys did.
//
// One column below the wide band (`Widths`): two columns of keys and words need the card's
// full thousand pixels, and a medium window halved them into lines of one word each. Taller
// than the window, the groups scroll inside the card.
//
// Every word comes from `ReaderCaptions.shortcutGroups()`, which takes them from `Words`; this
// only lays them out. When it opens, what may open it, and what closes it are `ReaderView`'s.

// No `pragma ComponentBehavior: Bound` here: the four key shapes are Components declared in
// the delegate and made by its Loader, which Qt 6.4 (Ubuntu 24.04) refuses in a bound file —
// « Cannot instantiate bound component outside its creation context » — and the card came
// up without its keys.

import QtQuick
import Leaf

Item {
    id: shortcuts

    /// Whether the card is up. It fades and scales in from 96 %, and out the same way.
    property bool open: false
    /// The book open is read right to left: ← and Ctrl ← go on, and are worded so.
    property bool rightToLeft: false
    readonly property bool narrow: Widths.band !== Widths.Band.Wide
    /// The card, so the view can tell a click on it from a click beside it.
    readonly property Item card: face

    /// The cross was pressed: the card asks to be closed, as Escape does.
    signal closeRequested()

    objectName: "reader-shortcuts"
    opacity: shortcuts.open ? 1 : 0
    visible: shortcuts.opacity > 0

    Behavior on opacity {
        // « Sans effet » on the settings card moves nothing, this card included.
        enabled: Preferences.motion !== Preferences.Instant

        NumberAnimation {
            duration: 180
            easing.type: Easing.OutCubic
        }
    }

    // Light: the page stays in sight behind it, as the mockup leaves it. The reader's own
    // paper, so the veil is the dark of the dark theme and the cream of the light one.
    Rectangle {
        objectName: "reader-shortcuts-veil"
        anchors.fill: parent
        color: Qt.rgba(Theme.readerPaper.r, Theme.readerPaper.g, Theme.readerPaper.b, 0.62)
    }

    /// A group's heading: emerald, in capitals, with its rule under it.
    component Heading: Item {
        id: heading

        property string text: ""

        width: parent ? parent.width : 0
        height: words.implicitHeight + Widths.scale.size(7)

        Text {
            id: words

            text: heading.text
            color: Theme.emerald
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            font.weight: Font.Medium
            font.capitalization: Font.AllUppercase
            font.letterSpacing: Widths.scale.letters(1.4)
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.rule
        }
    }

    /// One group: its heading, then its lines, keys on the left and what they do beside them.
    component Group: Column {
        id: group

        required property var modelData

        objectName: "reader-shortcuts-" + group.modelData.name
        spacing: 0
        bottomPadding: Widths.scale.size(14)

        Heading {
            text: group.modelData.title
        }

        Item {
            width: 1
            height: Widths.scale.size(8)
        }

        Repeater {
            model: group.modelData.rows

            Item {
                id: line

                required property var modelData

                width: group.width
                height: Math.max(Widths.scale.size(52),
                                 does.implicitHeight + Widths.scale.size(8))

                KeyRow {
                    id: keys

                    anchors.verticalCenter: parent.verticalCenter
                    width: Widths.scale.icon(232)
                    keys: line.modelData.keys
                }

                Text {
                    id: does

                    objectName: "reader-shortcut-does"
                    anchors.left: keys.right
                    anchors.leftMargin: Widths.scale.size(14)
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: line.modelData.does
                    color: Theme.ink
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(15)
                    lineHeight: 1.3
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Rectangle {
        id: face

        objectName: "reader-shortcuts-card"
        readonly property int sideways: Widths.scale.size(34)
        readonly property int above: Widths.scale.size(26)
        readonly property int between: Widths.scale.size(18)
        readonly property int below: Widths.scale.size(30)

        // Its drawn width so scaled, and never more than nine tenths of the window: wider, a
        // card about the keys hid the whole page it was laid over.
        width: Math.min(Widths.scale.column(1080), shortcuts.width * 0.9, shortcuts.width - 32)
        height: Math.min(face.above + head.height + face.between + body.height + face.below,
                         shortcuts.height - 32)
        anchors.centerIn: parent
        scale: 0.96 + 0.04 * shortcuts.opacity
        radius: Widths.scale.size(16)
        color: Theme.surface
        border.color: Theme.rule
        border.width: 1
        antialiasing: true

        CardLift {
            level: CardLift.Modal
        }

        Item {
            id: head

            x: face.sideways
            y: face.above
            width: face.width - 2 * face.sideways
            height: Widths.scale.size(32)

            Glyph {
                anchors.verticalCenter: parent.verticalCenter
                source: "assets/icons/tabler-keyboard.svg"
                side: Widths.scale.icon(28)
                tint: Theme.emerald
            }

            Text {
                objectName: "reader-shortcuts-title"
                x: Widths.scale.icon(28) + Widths.scale.size(12)
                anchors.verticalCenter: parent.verticalCenter
                text: ReaderCaptions.shortcuts
                color: Theme.ink
                font.family: Theme.displayFamily
                font.pixelSize: Widths.scale.title(28)
                font.weight: Font.DemiBold
            }

            IconButton {
                objectName: "reader-shortcuts-close"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                glyph: "tabler-x"
                width: Widths.scale.size(26)
                height: width
                side: Widths.scale.icon(22)
                tint: Theme.emerald
                label: ReaderCaptions.closeCard
                onTriggered: shortcuts.closeRequested()
            }
        }

        Flickable {
            id: scroller

            objectName: "reader-shortcuts-sheet"
            x: face.sideways
            y: head.y + head.height + face.between
            width: face.width - 2 * face.sideways
            height: face.height - scroller.y - face.below
            contentHeight: body.height
            clip: true
            interactive: body.height > scroller.height
            boundsBehavior: Flickable.StopAtBounds

            Grid {
                id: body

                objectName: "reader-shortcuts-grid"
                width: scroller.width
                columns: body.single ? 1 : 2
                columnSpacing: Widths.scale.size(56)
                rowSpacing: Widths.scale.size(6)

                /// One column below the wide band, and also wherever half the card leaves less
                /// than its keys and a line of words need: a card capped by the window had
                /// columns as cramped as the medium band's.
                readonly property bool single: shortcuts.narrow
                                               || (body.width - body.columnSpacing) / 2
                                                  < Widths.scale.column(420)
                readonly property real column: body.single
                                               ? body.width : (body.width - body.columnSpacing) / 2

                // In twos: the left column then the right, or one after the other.
                Repeater {
                    model: {
                        const all = ReaderCaptions.shortcutGroups(shortcuts.rightToLeft)
                        return [all.slice(0, 2), all.slice(2)]
                    }

                    Column {
                        id: column

                        required property var modelData

                        width: body.column

                        Repeater {
                            model: column.modelData

                            Group {
                                width: column.width
                            }
                        }
                    }
                }
            }
        }
    }
}
