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
    required property real leftInset

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

        running: skeleton.visible
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

    Column {
        x: skeleton.leftInset
        width: skeleton.width - 2 * skeleton.leftInset
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

                        Rectangle {
                            y: skeleton.coverHeight + 12
                            width: skeleton.coverWidth * 0.72
                            height: 12
                            radius: 4
                            color: Theme.onPaper
                            opacity: skeleton.pulse
                        }

                        Rectangle {
                            y: skeleton.coverHeight + 32
                            width: skeleton.coverWidth * 0.42
                            height: 10
                            radius: 4
                            color: Theme.onPaper
                            opacity: skeleton.pulse * 0.7
                        }
                    }
                }
            }
        }
    }
}
