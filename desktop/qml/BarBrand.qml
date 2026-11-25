// The name at the left of the bar: the mark, and the word beside it.
//
// One file because two bars drew it: the application's and the linking screen's, which had
// copied the first one's 36, 10 and 25 and would have kept them the day the bar changed
// (spacing audit, 23/10/2026).

import QtQuick
import Leaf

Item {
    id: brand

    /// Whether the word is drawn beside the mark. The application's bar drops it below the
    /// wide band, where its width is the field's.
    property bool worded: true

    // Whole pixels: the row places what follows at a whole pixel anyway, and a name a
    // fraction wide left the next thing a fraction off the gap it was given.
    implicitWidth: Math.ceil(mark.width
                             + (word.visible ? Widths.space.label + word.implicitWidth : 0))
    implicitHeight: mark.height

    Image {
        id: mark

        objectName: "leaf-mark"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: Widths.scale.icon(36)
        height: width
        source: Theme.dark
                ? "assets/logos/leaf-mark-dark.svg"
                : "assets/logos/leaf-mark-light.svg"
        sourceSize: Qt.size(width, height)
    }

    Text {
        id: word

        objectName: "brand-word"
        anchors.left: mark.right
        anchors.leftMargin: Widths.space.label
        anchors.verticalCenter: parent.verticalCenter
        visible: brand.worded
        textFormat: Text.PlainText
        text: "Leaf"
        color: Theme.ink
        font.family: Theme.displayFamily
        font.pixelSize: Widths.scale.title(25)
        font.weight: Font.Bold
    }
}
