// What the shelf looks like before it has anything to show.
//
// A spinner in the middle of an empty page says "something is happening" and nothing else:
// it does not say what is coming, how much of it, or where it will land, so the page jumps
// the moment it arrives. These are the shape of the answer — cover, title, second line, in
// the grid's own cells — so the first real tile lands where its placeholder already was.
//
// Only ever drawn when there is genuinely nothing to keep. A reader changing a filter keeps
// the shelf they had until the new one arrives, and never sees this.

import QtQuick
import Leaf

Item {
    id: skeleton

    required property real cellWidth
    required property real cellHeight
    required property real coverWidth
    required property real coverHeight
    /// The room a real tile keeps left of and above its cover, for its focus ring: given to
    /// the placeholders too, so the first cover lands exactly where its shape stood.
    required property real clearance

    objectName: "shelf-skeleton"

    /// Enough to fill the height on screen and not one more: a placeholder below the fold is
    /// work done for nobody, and the count is only ever a guess at what the page holds.
    readonly property int columns: Math.max(1, Math.floor(width / Math.max(1, cellWidth)))
    readonly property int rows: Math.max(1, Math.ceil(height / Math.max(1, cellHeight)))

    // One animation for the whole shelf rather than one per placeholder: twenty cells each
    // running their own made the sweep ripple, which reads as twenty things loading
    // separately instead of one page on its way.
    SequentialAnimation {
        id: breath

        running: skeleton.visible && Preferences.motion !== Preferences.Instant
        loops: Animation.Infinite

        NumberAnimation {
            target: skeleton
            property: "pulse"
            from: 0.55
            to: 1.0
            duration: 760
            easing.type: Easing.InOutSine
        }

        NumberAnimation {
            target: skeleton
            property: "pulse"
            from: 1.0
            to: 0.55
            duration: 760
            easing.type: Easing.InOutSine
        }
    }

    property real pulse: 1.0

    TileLines {
        id: lines
    }

    Column {
        x: skeleton.clearance
        y: skeleton.clearance
        width: skeleton.width - 2 * skeleton.clearance
        spacing: 0

        Repeater {
            model: skeleton.rows

            Row {
                spacing: 0

                Repeater {
                    model: skeleton.columns

                    Item {
                        width: skeleton.cellWidth
                        height: skeleton.cellHeight

                        Rectangle {
                            objectName: "skeleton-cover"
                            width: skeleton.coverWidth
                            height: skeleton.coverHeight
                            radius: Theme.coverRadius
                            color: Theme.onPaper
                            opacity: skeleton.pulse
                        }

                        // Where the name's first line and the count will land, from the
                        // same lines the tile draws: placeholders drawn at 12 and 32 under
                        // the cover stood where the words of 1100 used to be, not the ones
                        // a smaller window or a larger text size gives.
                        Rectangle {
                            y: skeleton.coverHeight + lines.above
                               + Math.round((lines.titleLine - height) / 2)
                            width: skeleton.coverWidth * 0.72
                            height: Math.round(lines.titleLine * 0.6)
                            radius: Widths.scale.size(4)
                            color: Theme.onPaper
                            opacity: skeleton.pulse
                        }

                        Rectangle {
                            y: skeleton.coverHeight + lines.above + lines.titleLine
                               + lines.between + Math.round((lines.countLine - height) / 2)
                            width: skeleton.coverWidth * 0.42
                            height: Math.round(lines.countLine * 0.55)
                            radius: Widths.scale.size(4)
                            color: Theme.onPaper
                            opacity: skeleton.pulse * 0.7
                        }
                    }
                }
            }
        }
    }
}
