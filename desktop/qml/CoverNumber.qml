// A volume's number, written over the foot of the series' cover it borrows.
//
// A webtoon is downloaded as a hundred and twenty chapters and none of them has a cover: each
// tile showed its opening panel, then the series' picture, a hundred and twenty times — a wall
// with nothing to tell one tile by. The number is what tells them (decision of 24/11/2026),
// large, at the bottom left, on the full veil. In an edition that holds both kinds, a word
// above it says which; in one that holds one kind, the word would only repeat itself.
//
// White, or dark over a picture whose foot is pale even veiled: `CoverTone` says which.

import QtQuick
import Leaf

Column {
    id: mark

    required property string number
    /// « TOME », « CHAPITRE », or empty.
    required property string word
    /// What lies under it is light: written dark.
    required property bool light
    required property real coverWidth

    // The mockup's measures at a cover 150 wide, as shares of it.
    readonly property real unit: mark.coverWidth / 150
    // `Theme.ink` of the light theme, which reads on a picture whatever the theme around it:
    // `CoverTone` weighs the contrast against this same dark.
    readonly property color ink: mark.light ? "#1a1d1b" : "#ffffff"

    anchors.left: parent.left
    anchors.bottom: parent.bottom
    anchors.leftMargin: Math.round(10 * mark.unit)
    anchors.bottomMargin: Math.round(10 * mark.unit)
    spacing: -Math.round(2 * mark.unit)
    visible: mark.number.length > 0

    Text {
        objectName: "cover-word"
        visible: mark.word.length > 0
        textFormat: Text.PlainText
        text: mark.word
        color: Qt.rgba(mark.ink.r, mark.ink.g, mark.ink.b, 0.88)
        font.family: Theme.displayFamily
        font.weight: Font.Bold
        font.pixelSize: Math.round(14 * mark.unit)
        font.letterSpacing: 0.6 * mark.unit
    }

    Text {
        objectName: "cover-number"
        textFormat: Text.PlainText
        text: mark.number
        color: mark.ink
        font.family: Theme.displayFamily
        font.weight: Font.Bold
        font.pixelSize: Math.round(44 * mark.unit)
        lineHeightMode: Text.FixedHeight
        lineHeight: Math.round(44 * mark.unit)
    }
}
