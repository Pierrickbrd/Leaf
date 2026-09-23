// « And it is done », in the bottom corner of a finished cover.
//
// Bottom right, just above the bar. The top corner belongs to the three dots, and the two of
// them shared it: a check half under a button is a state one has to move the pointer away to
// read. Above the bar rather than on it — the bar says « how far » and this says « and it is
// done », which a fill rounded up to a hundred per cent would not prove.
//
// Shared by the shelf's tile and a series page's, which is why it is a file: see `ReadBar`.

import QtQuick
import Leaf

Rectangle {
    id: done

    required property string key
    required property bool finished
    /// The cover it sits on, which its size follows: see `Widths.markOn`.
    required property real coverWidth

    objectName: "finished-" + done.key
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    anchors.rightMargin: 6
    anchors.bottomMargin: 10
    width: Widths.markOn(done.coverWidth)
    height: width
    radius: 99
    visible: done.finished
    // The same dark the three dots sit on, for the same reason: what is underneath is an
    // illustration and it may be any colour at all. The emerald is the mark, not the disc —
    // drawn the other way round it was a green button stuck in the corner of every finished
    // cover.
    color: Qt.rgba(12 / 255, 16 / 255, 14 / 255, 0.74)
    antialiasing: true

    Glyph {
        anchors.centerIn: parent
        side: Math.round(done.width * 0.62)
        source: "assets/icons/check.svg"
        tint: Theme.emerald
    }
}
