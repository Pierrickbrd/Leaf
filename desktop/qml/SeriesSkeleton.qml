// The only place a series page has nothing to keep: the first opening from the shelf.
//
// It has the **shape of the answer**, at the right height, so the first real line lands where
// its stand-in was. The cover's stand-in is the size the cover will be — that is the whole
// point of a fixed size: the understudy knows the room.
//
// One breath for the whole block and not one per bar: twenty placeholders each pulsing for
// themselves make a Christmas tree. And nothing moves at all if the system asks for less
// animation, the way the shelf's own skeleton already does.

import QtQuick
import Leaf

Item {
    id: skeleton

    objectName: "series-skeleton"

    Row {
        anchors.fill: parent
        spacing: 18
        opacity: breath.running ? breath.value : 0.6

        Rectangle {
            width: 112
            height: 168
            radius: Theme.coverRadius
            color: Theme.onPaper
        }

        Column {
            spacing: 10
            y: 6

            // Of different widths, because a title, an edition and two lines of facts are not
            // the same length. Four identical bars look like nothing at all.
            Rectangle { width: 90; height: 11; radius: 5; color: Theme.onPaper }
            Rectangle { width: 230; height: 24; radius: 6; color: Theme.onPaper }
            Rectangle { width: 130; height: 12; radius: 5; color: Theme.onPaper }
            Item { width: 1; height: 6 }
            Rectangle { width: 280; height: 11; radius: 5; color: Theme.onPaper }
            Rectangle { width: 200; height: 11; radius: 5; color: Theme.onPaper }
        }
    }

    NumberAnimation {
        id: breath

        property real value: 0.6

        target: breath
        property: "value"
        from: 0.45
        to: 0.8
        duration: 900
        loops: Animation.Infinite
        easing.type: Easing.InOutSine
        running: skeleton.visible
    }
}
