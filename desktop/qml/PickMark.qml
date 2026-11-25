// « This one is picked », drawn on a cover: a ring round it and a check in its top-left corner.
//
// The ring alone is the focus's colour and shape; the check is what tells a picked cover from
// a focused one. Top left because the three dots hold the top right and the finished check the
// bottom right, and a mark that shared a corner would be half under another.

import QtQuick
import Leaf

Item {
    id: mark

    required property bool picked
    /// The cover it sits on, which the check's size follows: see `Widths.markOn`.
    required property real coverWidth

    anchors.fill: parent
    visible: mark.picked

    Rectangle {
        anchors.fill: parent
        radius: Theme.coverRadius
        color: "transparent"
        border.color: Theme.emerald
        border.width: 3
        antialiasing: true
    }

    Rectangle {
        x: 6
        y: 6
        width: Widths.markOn(mark.coverWidth)
        height: width
        radius: 99
        // The same dark the other marks sit on: the cover under it can be any colour.
        color: Qt.rgba(12 / 255, 16 / 255, 14 / 255, 0.74)
        antialiasing: true

        Glyph {
            anchors.centerIn: parent
            side: Math.round(parent.width * 0.62)
            source: "assets/icons/check.svg"
            tint: Theme.emerald
        }
    }
}
