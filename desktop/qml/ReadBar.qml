// How far through, along the bottom of a cover: the series on the shelf and the volume on a
// series page draw the same bar, and wrote it twice until the shelf's copy fell behind — it
// drew nothing once a series was finished, which is what « never opened » looks like too.
// The design says a full bar for finished precisely so that the two stop reading the same.
//
// Placed inside the rounded cover, which clips it to the corners it runs along, over the
// cover's veil (`CoverVeil`), which is its ground: the bar's own shade went with it.

import QtQuick
import Leaf

Item {
    id: bar

    /// What the parts are named after, for a test to find them: a series or an entry id.
    required property string key
    required property bool started
    required property bool finished
    required property real howFarRead
    /// On a cover, the vivid emerald of both themes (`Theme.coverEmerald`); on paper — the
    /// volume panel, the file's details — the theme's own.
    property bool onCover: true

    anchors.fill: parent
    visible: bar.started || bar.finished

    Rectangle {
        objectName: "in-progress-" + bar.key
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 4
        // The alpha is in the colour, never in `opacity`: `opacity` on an item is applied to
        // everything it holds, so a track at 0.45 was drawing the emerald inside it at 0.45
        // too. The design says `rgba(0,0,0,.45)` for exactly this reason — the track is what
        // is translucent, and what it measures is not.
        color: Qt.rgba(0, 0, 0, 0.45)

        Rectangle {
            objectName: "read-so-far-" + bar.key
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            // Full when finished whatever the count says, and never so short it cannot be
            // seen when merely started: a first page turned is still « started ».
            width: parent.width * (bar.finished ? 1 : Math.max(0.04, bar.howFarRead))
            color: bar.onCover ? Theme.coverEmerald : Theme.emerald
        }
    }
}
