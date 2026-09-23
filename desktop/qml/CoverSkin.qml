// What every cover in this client wears on top of itself.
//
// Two hairlines, drawn inside the rounding: a dark one all round, and a pale one along the
// top edge alone. The dark one keeps a pale cover from bleeding into the page; the pale one
// is the light a physical book catches on its top edge, and it is what makes a cover read as
// an object laid on the page rather than a picture printed into it.
//
// One file because it is on the header's cover, on a shelf tile, on a volume tile and in the
// edition menu — and a fifth copy of two rectangles is a fifth place to forget one of them.

import QtQuick
import Leaf

Item {
    id: skin

    property int radius: Theme.coverRadius

    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        radius: skin.radius
        color: "transparent"
        border.color: "#000000"
        border.width: 1
        opacity: 0.35
        antialiasing: true
    }

    // The top edge only, and inside the border: a line drawn round the whole shape would be
    // a second outline, where what is wanted is a single catch of light.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: skin.radius / 2
        anchors.rightMargin: skin.radius / 2
        anchors.topMargin: 1
        height: 1
        color: "#FFFFFF"
        opacity: 0.10
    }
}
