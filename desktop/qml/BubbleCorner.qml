// Where the bubbles are laid: a screen divided in six, and the chosen zone filled.
//
// One chooses a *place*, so the control is a place. Six positions as pills would have
// overflowed `SettingsChoice`, whose own header says three answers fit on a line — and a
// picture does not suffer that limit, because it is not read as a list.
//
// **The zones are the screen.** Their outer edges are its edges, their inner edges are shared,
// and the rounding cuts the four corners. Which is why the mark on the chosen one is painted
// rather than laid on: a rectangle with four rounded corners floats *inside* a zone, where
// what is wanted is the zone itself lighting up — square where it meets its neighbours, round
// where it meets the screen's own corner.

import QtQuick
import Leaf

Item {
    id: screen

    /// `Preferences.Corner`, as the model spells it.
    required property int chosen
    required property string label

    signal picked(int where)

    readonly property int rounding: 9

    implicitHeight: frame.height + 8 + word.implicitHeight
    height: implicitHeight

    Item {
        id: frame

        // A thumbnail and not a mock-up: drawn the width of its card it filled the whole of
        // it to say one word. Sixteen by ten, which is what a screen is.
        width: 220
        height: Math.round(width * 10 / 16)

        // The shadow a screen casts on the card it sits on.
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 4
            radius: screen.rounding
            color: "#000000"
            opacity: Theme.dark ? 0.5 : 0.16
        }

        Rectangle {
            id: glass

            anchors.fill: parent
            radius: screen.rounding
            // The paper of the application itself, which is what a Leaf window shows.
            color: Theme.paper
            antialiasing: true

            // The light along the top edge, the one the covers carry.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: screen.rounding
                anchors.rightMargin: screen.rounding
                anchors.topMargin: 1
                height: 1
                color: "#FFFFFF"
                opacity: 0.07
            }

            Grid {
                anchors.fill: parent
                columns: 3
                rows: 2

                Repeater {
                    model: 6

                    Item {
                        id: zone

                        required property int index

                        readonly property bool here: zone.index === screen.chosen
                        /// Which of its four corners are the screen's own. The middle two of
                        /// each row have none, and a mark rounded on all four inside one of
                        /// them reads as something laid on the screen rather than as a part
                        /// of it.
                        readonly property bool topLeft: zone.index === 0
                        readonly property bool topRight: zone.index === 2
                        readonly property bool bottomLeft: zone.index === 3
                        readonly property bool bottomRight: zone.index === 5

                        width: glass.width / 3
                        height: glass.height / 2

                        TapHandler { onTapped: screen.picked(zone.index) }
                        HoverHandler {
                            id: pointer

                            cursorShape: Qt.PointingHandCursor
                        }

                        Canvas {
                            id: paint

                            anchors.fill: parent
                            antialiasing: true

                            Connections {
                                target: zone
                                function onHereChanged() { paint.requestPaint() }
                            }

                            Connections {
                                target: pointer
                                function onHoveredChanged() { paint.requestPaint() }
                            }

                            onPaint: {
                                const context = getContext("2d")
                                context.reset()
                                if (!zone.here && !pointer.hovered)
                                    return

                                const r = screen.rounding
                                const w = width
                                const h = height
                                // Half a pixel in, so the stroke lands on the pixel rather
                                // than across two of them.
                                const a = 0.5
                                context.beginPath()
                                context.moveTo(zone.topLeft ? r : a, a)
                                context.lineTo(zone.topRight ? w - r : w - a, a)
                                if (zone.topRight)
                                    context.quadraticCurveTo(w - a, a, w - a, r)
                                context.lineTo(w - a, zone.bottomRight ? h - r : h - a)
                                if (zone.bottomRight)
                                    context.quadraticCurveTo(w - a, h - a, w - r, h - a)
                                context.lineTo(zone.bottomLeft ? r : a, h - a)
                                if (zone.bottomLeft)
                                    context.quadraticCurveTo(a, h - a, a, h - r)
                                context.lineTo(a, zone.topLeft ? r : a)
                                if (zone.topLeft)
                                    context.quadraticCurveTo(a, a, r, a)
                                context.closePath()

                                if (zone.here) {
                                    const wash = context.createLinearGradient(0, 0, w, h)
                                    wash.addColorStop(0, Qt.rgba(0.184, 0.725, 0.545, 0.34))
                                    wash.addColorStop(1, Qt.rgba(0.184, 0.725, 0.545, 0.12))
                                    context.fillStyle = wash
                                    context.fill()
                                    context.lineWidth = 1
                                    context.strokeStyle = Qt.rgba(0.184, 0.725, 0.545, 0.75)
                                    context.stroke()
                                    return
                                }
                                // Merely under the pointer: the faintest possible answer, so
                                // that moving across the six does not look like choosing.
                                context.fillStyle = Qt.rgba(1, 1, 1, 0.04)
                                context.fill()
                            }
                        }

                        // A hair at six per cent, not the ordinary rule: at its usual value
                        // the grid shone brighter than the chosen zone, and it is the zone
                        // one comes to read.
                        Rectangle {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 1
                            visible: zone.index % 3 !== 2
                            color: "#FFFFFF"
                            opacity: 0.055
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            visible: zone.index < 3
                            color: "#FFFFFF"
                            opacity: 0.055
                        }
                    }
                }
            }
        }
    }

    // The picture says where; the word is what a screen reader announces and what makes the
    // setting findable by somebody searching for it. Under the screen and against the card's
    // own left edge, like every other line of the card.
    Text {
        id: word

        anchors.left: frame.left
        anchors.top: frame.bottom
        anchors.topMargin: 8
        text: screen.label
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: 12
    }
}
