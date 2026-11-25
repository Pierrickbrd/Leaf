// « And it is done », beside the words under a finished cover.
//
// Out of the cover since 24/11/2026: in its bottom corner it sat on the veil every cover now
// has, beside a volume's number, and on the bar it stood above — three marks in one corner of
// a picture. Beside the name and its count it is a word of the caption, a green pill at the
// right, centred on the two lines; the tile places it and narrows its words to leave it room.
//
// Shared by the shelf's tile and a series page's, which is why it is a file: see `ReadBar`.

import QtQuick
import Leaf

Rectangle {
    id: done

    required property string key
    required property bool finished

    objectName: "finished-" + done.key
    width: Widths.scale.size(18)
    height: width
    radius: width / 2
    visible: done.finished
    color: Theme.emerald
    antialiasing: true

    Glyph {
        anchors.centerIn: parent
        side: Math.round(done.width * 0.72)
        source: "assets/icons/check.svg"
        tint: Theme.onEmerald
    }
}
