// One tile of the last tab: a cover, a name, and what it is underneath.
//
// Not the shelf's tile made smaller, which is what this was. The design draws its own object
// here — a seven-pixel radius rather than a cover's twelve, the name in soft ink rather than
// in full ink, three lines rather than two — because these are read one after another to
// choose where to go next, where the shelf's are glanced at across a wall.
//
// The one being read carries a thin emerald outline held two pixels off the cover, and not
// the ring a keyboard leaves: that ring means « the focus is here », and this means « you are
// here ». Two facts, two marks.

import QtQuick
import Leaf

Item {
    id: tile

    required property string seriesId
    required property string name
    required property string detail
    required property string cover
    required property bool here
    required property real side

    signal opened()

    objectName: "tile-" + seriesId
    width: side
    height: side * 1.5 + 6 + words.implicitHeight

    Accessible.role: Accessible.ListItem
    Accessible.name: name
    Accessible.description: detail

    HoverHandler {
        id: pointer

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: tile.opened()
    }

    scale: pointer.hovered ? 1.04 : 1.0
    Behavior on scale {
        NumberAnimation {
            duration: 90
            easing.type: Easing.OutQuad
        }
    }

    Item {
        id: frame

        width: tile.side
        height: tile.side * 1.5

        CoverShadow {
            under: "mini-" + tile.detail
        }

        RoundedCover {
            anchors.fill: parent
            source: tile.cover
            radius: 7
        }

        CoverSkin { radius: 7 }

        // Held off the cover rather than drawn on it: laid on the edge it read as part of
        // the illustration, and half the covers in a library have a border of their own.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: 9
            color: "transparent"
            border.color: Theme.emerald
            border.width: 2
            visible: tile.here
            antialiasing: true
        }
    }

    Column {
        id: words

        anchors.left: frame.left
        anchors.right: frame.right
        anchors.top: frame.bottom
        anchors.topMargin: 6
        spacing: 1

        Text {
            width: parent.width
            text: tile.name
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 12
            lineHeight: 1.2
            lineHeightMode: Text.ProportionalHeight
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            text: tile.detail
            visible: text.length > 0
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: 11
            elide: Text.ElideRight
        }
    }
}
