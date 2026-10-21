// The ISBN of the open volume, as the barcode on the back of the book, on a key that copies it.
//
// The owner's choice of 22/10/2026, from four drawn side by side: the one fact of a volume
// that is carried elsewhere — into a bookseller's search, a library's catalogue — had been a
// line of text among the others, with nothing to say it could be taken. On an emerald key, the
// last thing in the panel after the chapters (see `ReaderVolumePanel`), it reads as the thing
// on a book that is scanned, and as a key that is pressed: a click, Enter or Space copies the
// thirteen digits and a bubble says so, since a copy changes nothing on the screen.
//
// **The key is the shortcuts card's** (`Keycap`): a lip showing six pixels under the face, and
// `CardLift`'s shadow, never a second darker shape. Pressed, the face goes down half its lip,
// as a key does. Its colours are `Theme.barcode*` and not the emerald's: in the light theme
// the emerald is dark and what is written on it light, and a barcode drawn light on dark is
// read inverted or not at all (ruling of 22/10/2026). Dark bars on a light ground in both
// themes; the dark theme's key is the emerald one approved.
//
// **The bars are `Isbn`'s**, one rounded rectangle per run of dark modules, the guards longer;
// no digits under them, since the number is written beside them at a size that can be read.
// A module is a whole number of the screen's pixels — 1.6 drawn at 1280, scaled with the
// window, rounded up — because a bar a pixel and a half wide is drawn as a blurred two, and
// the pattern is no longer the one encoded. Up and not to the nearest: rounded, the window of
// 1100 got one pixel and a barcode half the width of the number beside it. Never wider than
// the face has room for, and never under one pixel of the screen.
//
// **The number goes under the bars when it no longer fits beside them**, on a panel too
// narrow for both, rather than the bars shrinking past legible.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Rectangle {
    id: key

    /// `Isbn.barcode(…)`: `{ digits, lines, bars }`. The panel draws no key without one.
    property var code: ({})

    readonly property int lip: Widths.scale.size(6)
    readonly property int pad: Widths.scale.size(14)
    readonly property int gap: Widths.scale.size(14)
    readonly property int barHeight: Widths.scale.size(70)
    readonly property int guardHeight: key.barHeight + Widths.scale.size(9)
    readonly property real ratio: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    /// Room inside the face for the bars, alone on their line.
    readonly property real room: key.width - 2 * key.pad
    /// One module, in whole pixels of the screen: drawn at 1.6 and rounded up, never wider
    /// than the room allows, never under one.
    readonly property real module: Math.max(1, Math.min(
                                                Math.ceil(1.6 * Widths.controls * key.ratio),
                                                Math.floor(key.room * key.ratio / 95)))
                                   / key.ratio
    /// The number beside the bars, or under them when both do not fit across.
    readonly property bool beside: key.room >= bars.width + key.gap + meta.implicitWidth
    readonly property bool pressed: tap.pressed

    function copy() {
        if (!key.code.digits)
            return
        Isbn.copy(key.code.digits)
        Toasts.isbnCopied(key.code.digits)
    }

    implicitHeight: face.height + key.lip
    height: implicitHeight
    radius: Widths.scale.size(12)
    color: Theme.barcodeLip
    antialiasing: true
    activeFocusOnTab: visible

    Accessible.role: Accessible.Button
    Accessible.name: ReaderCaptions.factIsbn
    Accessible.description: ReaderCaptions.copyIsbn
    Accessible.focusable: true
    Accessible.focused: key.activeFocus
    Accessible.onPressAction: key.copy()

    CardLift {
        level: CardLift.Card
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tap

        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: key.copy()
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            key.copy()
            event.accepted = true
        }
    }

    FocusRing {
        objectName: "reader-volume-isbn-key-focus"
        cornerRadius: key.radius
    }

    Rectangle {
        id: face

        objectName: "reader-volume-isbn-face"
        width: parent.width
        height: 2 * key.pad + (key.beside ? Math.max(bars.height, meta.implicitHeight)
                                          : bars.height + key.gap + meta.implicitHeight)
        y: key.pressed ? Math.round(key.lip / 2) : 0
        radius: key.radius
        color: Theme.barcodeFace
        antialiasing: true

        Item {
            id: bars

            objectName: "reader-volume-isbn-bars"
            x: key.pad
            y: key.beside ? Math.round((face.height - bars.height) / 2) : key.pad
            width: 95 * key.module
            height: key.guardHeight

            Repeater {
                model: key.code.bars || []

                Rectangle {
                    required property var modelData

                    x: modelData.from * key.module
                    width: modelData.width * key.module
                    height: modelData.guard ? key.guardHeight : key.barHeight
                    // Rounded ends, half a module: a bar one module wide is a pill, a wider
                    // one keeps its flat sides.
                    radius: key.module / 2
                    color: Theme.barcodeInk
                    antialiasing: true
                }
            }
        }

        Column {
            id: meta

            objectName: "reader-volume-isbn-number"
            x: key.beside ? key.width - key.pad - meta.width : key.pad
            y: key.beside ? Math.round((face.height - meta.height) / 2)
                          : bars.y + bars.height + key.gap
            spacing: Widths.scale.size(4)

            Text {
                objectName: "reader-volume-isbn-label"
                text: ReaderCaptions.factIsbn
                color: Theme.barcodeInk
                opacity: 0.8
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(11)
                font.weight: Font.Medium
                font.capitalization: Font.AllUppercase
                font.letterSpacing: Widths.scale.letters(1.6)
            }

            // The two lines of the number held together, as one number set on two lines
            // and not two facts: apart by the column's spacing, they read as a list.
            Column {
                Repeater {
                    model: key.code.lines || []

                    Text {
                        required property int index
                        required property string modelData

                        objectName: "reader-volume-isbn-line-" + index
                        text: modelData
                        color: Theme.barcodeInk
                        font.family: Theme.displayFamily
                        font.pixelSize: Widths.scale.title(19)
                        font.weight: Font.DemiBold
                        font.letterSpacing: Widths.scale.letters(0.5)
                        lineHeight: 0.95
                        textFormat: Text.PlainText
                    }
                }
            }

            Text {
                objectName: "reader-volume-isbn-hint"
                text: ReaderCaptions.copyIsbn
                color: Theme.barcodeInk
                opacity: 0.8
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(11)
            }
        }
    }
}
