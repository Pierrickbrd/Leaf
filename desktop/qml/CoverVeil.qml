// The shade at the foot of every cover, shelf and series page alike.
//
// Two strengths, one decision (24/11/2026). Faint under a cover of its own, where it only
// gives the bar a ground: the bar had a shade of its own, fifteen pixels tall, which a green
// cover still swallowed. Full under a borrowed one, the series' picture standing in for a
// volume, where it carries the volume's number: half the cover, black to 72 % at the foot.
// `CoverTone` measures the picture through the full strength and keeps its own copy of it.
//
// Placed inside the rounded cover, which clips it to the corners it runs along.

import QtQuick

Rectangle {
    id: veil

    /// Under a number: half the cover, and darker.
    property bool full: false

    anchors.left: parent.left
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    // 64 of 224 and 112 of 224, the mockup's: a share of the cover, whatever its size.
    height: parent.height * (veil.full ? 0.5 : 64 / 224)
    gradient: Gradient {
        GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0) }
        GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, veil.full ? 0.72 : 0.38) }
    }
}
