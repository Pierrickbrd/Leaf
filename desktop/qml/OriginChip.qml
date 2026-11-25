// Where a value came from, as a small rounded word beside it: Dit, Repris, Deviné, Manquant.
//
// Four looks and no fifth, each from the theme's own tokens, so that the dark theme is decided
// where every other colour of the client is: said is the emerald on its wash, taken an emerald
// outline round nothing, guessed the ink on a card's surface, missing the alert on its wash.
// Guessed also wears the theme's rule as its edge: on the window's paper a surface is one shade
// off, and without an edge the chip was a word floating beside its field.
//
// The word is the caller's — a field's origin, or the name of the field that makes a whole line
// worst — and the look is the origin's, never the word's: comparing French strings here is
// what `Review::Origin` exists to prevent.

import QtQuick
import Leaf

Rectangle {
    id: chip

    /// `Review::Origin`: 0 said, 1 taken, 2 guessed, 3 missing.
    required property int origin
    required property string label

    readonly property int said: 0
    readonly property int taken: 1
    readonly property int guessed: 2
    readonly property int missing: 3

    /// Room kept after the word for a mark the caller draws there — the ▾ of a role that can
    /// change — so the mark sits inside the chip's own edge and not against it.
    property real trail: 0
    property int textSize: Widths.scale.caption(12)
    /// The ink of the word, for a mark drawn beside it in the same colour.
    readonly property color ink: word.color

    implicitWidth: word.implicitWidth + trail + 2 * Widths.scale.fit(9)
    implicitHeight: Math.max(Widths.scale.fit(22), word.implicitHeight + Widths.scale.fit(6))
    width: implicitWidth
    height: implicitHeight
    radius: height / 2
    antialiasing: true
    color: chip.origin === chip.said ? Theme.emeraldWash
         : chip.origin === chip.missing ? Theme.alertWash
         : chip.origin === chip.guessed ? Theme.surface
                                        : "transparent"
    border.width: chip.origin === chip.taken || chip.origin === chip.guessed ? 1 : 0
    border.color: chip.origin === chip.taken ? Theme.emerald : Theme.rule

    Accessible.role: Accessible.StaticText
    Accessible.name: chip.label

    Text {
        id: word

        objectName: chip.objectName + "-text"
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: -chip.trail / 2
        text: chip.label
        textFormat: Text.PlainText
        color: chip.origin === chip.missing ? Theme.alert
             : chip.origin === chip.guessed ? Theme.ink
                                            : Theme.emerald
        font.family: Theme.textFamily
        font.pixelSize: chip.textSize
        font.weight: Font.DemiBold
    }
}
