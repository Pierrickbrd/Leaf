// The keys' focus on « Qui lit ? »: three pixels round a profile's square, concentric with it and
// `room` pixels past it, in the profile's colour.
//
// Placed in the square's own item and declared before the square, so the crown or the lock
// overhanging its corner stands over the ring and hides the stretch it would cross.

import QtQuick
import Leaf

Rectangle {
    id: ring

    required property real side
    required property real squareRadius
    property real room: 8
    /// Lit for the keys' focus, and hidden, as every ring, once the pointer has taken over.
    property bool focused: false
    readonly property var cursor: Window.window ? Window.window.navigationCursor : null

    visible: focused && !(cursor && cursor.pointerTookOver)
    anchors.centerIn: parent
    width: side + 2 * room
    height: width
    radius: squareRadius + room
    color: "transparent"
    border.color: Theme.emerald
    border.width: 3
    antialiasing: true
}
