// The thin mark on the right edge of a strip: how much of the chapter is behind, and a handle
// to go elsewhere in it.
//
// A strip has no fixed page, so the scrub and « 1 / 29 » of a book said nothing of where the
// reader was (maquette Figma « Bande du bas · lecteur webtoon », 24/11/2026). The mark says it
// as a share of the chapter, comes and goes with the bars, and is grabbed or clicked to jump;
// the share it points at is said in a bubble beside the handle while the pointer is on it.

import QtQuick
import Leaf

Item {
    id: mark

    /// The strip it follows and moves (`ReaderStrip`).
    required property var strip
    /// Where the pointer is along the mark, 0 to 1, while it is over it or dragging.
    readonly property real pointed: Math.max(0, Math.min(1, (area.mouseY - track.y)
                                                         / Math.max(1, track.height)))
    readonly property real shown: area.containsMouse || area.pressed ? mark.pointed
                                                                      : mark.strip.fraction

    objectName: "reader-strip-mark"
    // Wider than it is drawn, so that a hand finds it.
    width: Widths.scale.size(20)

    Rectangle {
        id: track

        objectName: "reader-strip-mark-track"
        anchors.horizontalCenter: parent.horizontalCenter
        width: Widths.scale.size(3)
        height: parent.height
        radius: width / 2
        color: Theme.rule
    }

    Rectangle {
        objectName: "reader-strip-mark-read"
        anchors.horizontalCenter: track.horizontalCenter
        width: track.width
        height: track.height * mark.strip.fraction
        radius: width / 2
        color: Theme.emerald
    }

    Rectangle {
        id: handle

        objectName: "reader-strip-mark-handle"
        anchors.horizontalCenter: track.horizontalCenter
        width: Widths.scale.size(5)
        height: Widths.scale.size(36)
        y: (track.height - handle.height) * mark.strip.fraction
        radius: width / 2
        color: Theme.emerald
    }

    Rectangle {
        objectName: "reader-strip-mark-bubble"
        visible: area.containsMouse || area.pressed
        anchors.right: track.left
        anchors.rightMargin: Widths.scale.room(10)
        y: Math.max(0, Math.min(mark.height - height,
                                (track.height - handle.height) * mark.shown
                                + (handle.height - height) / 2))
        width: share.implicitWidth + 2 * Widths.scale.room(8)
        height: share.implicitHeight + 2 * Widths.scale.room(4)
        radius: height / 2
        color: Theme.surface

        Text {
            id: share

            anchors.centerIn: parent
            textFormat: Text.PlainText
            text: ReaderCaptions.zoom(mark.shown)
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(11)
        }
    }

    // Pressed or dragged, the strip goes there: a hand on the mark is a hand on the strip.
    MouseArea {
        id: area

        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: Qt.PointingHandCursor
        onPressed: mark.strip.jumpTo(mark.pointed)
        onPositionChanged: {
            if (area.pressed)
                mark.strip.jumpTo(mark.pointed)
        }
    }
}
