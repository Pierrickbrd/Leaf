// Where the bubbles are laid: a screen divided in six, and the chosen zone filled.
//
// One chooses a *place*, so the control is a place. Six positions as pills would have
// overflowed `SettingsChoice`, whose own header says three answers fit on a line — and a
// picture does not suffer that limit, because it is not read as a list.
//
// **The zones are the screen.** Their outer edges are its edges, their inner edges are shared,
// and the rounding cuts the four corners. Which is why the mark on the chosen one is the zone
// itself lighting up — square where it meets its neighbours, round where it meets the
// screen's own corner — and not a rectangle with four rounded corners floating inside it.
//
// **Every line is one pixel of the screen it is shown on**, not one unit of the layout, and
// only the four arcs are smoothed. At a fractional scale — 1.12 on the laptop this was drawn
// for — a line one unit wide covers a pixel and a bit and lands on one or two depending on
// where it falls, and a smoothed straight edge that falls between two pixels is spread over
// both at half strength: the hairs came out in two thicknesses, one of them vanished, and the
// green outline along the screen's edge read paler than along a neighbour. A straight line
// one device pixel wide and not smoothed covers exactly one pixel wherever it falls. So every
// straight piece is that, and an arc is the corner of a rounded outline cut out to its square.

import QtQuick
import Leaf

Item {
    id: screen

    /// `Preferences.Corner`, as the model spells it.
    required property int chosen
    required property string label

    signal picked(int where)

    readonly property int rounding: 9
    /// One pixel of the screen, whatever the scale: see the header.
    readonly property real hair: Theme.hairOf(Window.window)
    /// The lines that divide the screen and close it. One colour for both, so the edge reads
    /// as the last of the hairs rather than as a frame round them.
    readonly property color hairColor: Theme.dark ? Qt.rgba(1, 1, 1, 0.09)
                                                  : Qt.rgba(0, 0, 0, 0.10)
    readonly property color edge: Qt.rgba(0.184, 0.725, 0.545, 0.75)

    implicitHeight: frame.height + 8 + word.implicitHeight
    height: implicitHeight

    /// A rectangle's outline drawn inside its bounds, one hair wide: straight sides that are
    /// not smoothed, and an arc at each corner that is rounded. A corner left square has its
    /// two sides run on into it.
    component Outline: Item {
        id: outline

        property color color
        property bool topLeft: false
        property bool topRight: false
        property bool bottomLeft: false
        property bool bottomRight: false

        readonly property real r: screen.rounding

        Rectangle {
            x: outline.topLeft ? outline.r : 0
            width: parent.width - x - (outline.topRight ? outline.r : 0)
            height: screen.hair
            color: outline.color
        }
        Rectangle {
            x: outline.bottomLeft ? outline.r : 0
            y: parent.height - height
            width: parent.width - x - (outline.bottomRight ? outline.r : 0)
            height: screen.hair
            color: outline.color
        }
        Rectangle {
            y: outline.topLeft ? outline.r : 0
            width: screen.hair
            height: parent.height - y - (outline.bottomLeft ? outline.r : 0)
            color: outline.color
        }
        Rectangle {
            x: parent.width - width
            y: outline.topRight ? outline.r : 0
            width: screen.hair
            height: parent.height - y - (outline.bottomRight ? outline.r : 0)
            color: outline.color
        }

        Repeater {
            model: [outline.topLeft, outline.topRight, outline.bottomLeft, outline.bottomRight]

            // The corner of a rounded outline twice its radius, seen through a square of it.
            Item {
                required property bool modelData
                required property int index

                readonly property bool rightHand: index % 2 === 1
                readonly property bool lowerHalf: index >= 2

                visible: modelData
                x: rightHand ? outline.width - outline.r : 0
                y: lowerHalf ? outline.height - outline.r : 0
                width: outline.r
                height: outline.r
                clip: true

                Rectangle {
                    x: parent.rightHand ? -outline.r : 0
                    y: parent.lowerHalf ? -outline.r : 0
                    width: 2 * outline.r
                    height: 2 * outline.r
                    radius: outline.r
                    color: "transparent"
                    border.width: screen.hair
                    border.color: outline.color
                    antialiasing: true
                }
            }
        }
    }

    Item {
        id: frame

        // A thumbnail and not a mock-up: drawn the width of its card it filled the whole of
        // it to say one word. Sixteen by ten, which is what a screen is — near enough that
        // three columns and two rows each come out whole: 74 by 69.
        width: 222
        height: 138

        Rectangle {
            id: glass

            anchors.fill: parent
            radius: screen.rounding
            // The paper of the application itself, which is what a Leaf window shows.
            color: Theme.paper
            antialiasing: true

            // The shadow a screen casts on the card it sits on.
            CardLift { level: CardLift.Bubble }
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
                    /// Which of its sides are the screen's own. The others are shared with a
                    /// neighbour, and the mark is square there.
                    readonly property bool atLeft: zone.index % 3 === 0
                    readonly property bool atRight: zone.index % 3 === 2
                    readonly property bool atTop: zone.index < 3
                    readonly property bool atBottom: zone.index >= 3

                    objectName: "bubble-corner-zone-" + zone.index
                    width: frame.width / 3
                    height: frame.height / 2
                    // A stop each, chosen with Enter or Space: the picture was the one
                    // setting of the tab the keyboard could not change.
                    activeFocusOnTab: true

                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                                || event.key === Qt.Key_Enter) {
                            screen.picked(zone.index)
                            event.accepted = true
                        }
                    }

                    FocusRing {
                        cornerRadius: 4
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: screen.picked(zone.index)
                    }
                    HoverHandler {
                        id: pointer

                        cursorShape: Qt.PointingHandCursor
                    }

                    // The zone lit: a rounded rectangle pushed past every side the zone shares,
                    // so that only the screen's own corner keeps its rounding.
                    Item {
                        anchors.fill: parent
                        clip: true
                        visible: zone.here || pointer.hovered

                        Rectangle {
                            readonly property int past: screen.rounding + 1

                            x: zone.atLeft ? 0 : -past
                            y: zone.atTop ? 0 : -past
                            width: parent.width + (zone.atLeft ? 0 : past)
                                   + (zone.atRight ? 0 : past)
                            height: parent.height + (zone.atTop ? 0 : past)
                                    + (zone.atBottom ? 0 : past)
                            radius: screen.rounding
                            antialiasing: true
                            // Merely under the pointer: the faintest possible answer, so that
                            // moving across the six does not look like choosing.
                            color: zone.here ? "transparent" : Qt.rgba(1, 1, 1, 0.04)
                            gradient: zone.here ? wash : null

                            Gradient {
                                id: wash

                                GradientStop {
                                    position: 0
                                    color: Qt.rgba(0.184, 0.725, 0.545, 0.34)
                                }
                                GradientStop {
                                    position: 1
                                    color: Qt.rgba(0.184, 0.725, 0.545, 0.12)
                                }
                            }
                        }
                    }

                    // The hairs between zones: each zone draws the one on its right and the one
                    // under it, straddling the seam, so the lit zone's outline can lie on them.
                    Rectangle {
                        visible: !zone.atRight
                        x: parent.width - screen.hair / 2
                        width: screen.hair
                        height: parent.height
                        color: screen.hairColor
                    }
                    Rectangle {
                        visible: !zone.atBottom
                        y: parent.height - screen.hair / 2
                        width: parent.width
                        height: screen.hair
                        color: screen.hairColor
                    }
                }
            }
        }

        // The screen's own edge, in the hairs' colour.
        Outline {
            anchors.fill: parent
            color: screen.hairColor
            topLeft: true
            topRight: true
            bottomLeft: true
            bottomRight: true
        }

        // The chosen zone's outline, over everything: along the screen's edge where the zone
        // meets it, on the hairs where it meets a neighbour.
        Outline {
            readonly property bool atLeft: screen.chosen % 3 === 0
            readonly property bool atRight: screen.chosen % 3 === 2
            readonly property bool atTop: screen.chosen < 3
            readonly property real zoneX: (screen.chosen % 3) * frame.width / 3
            readonly property real zoneY: atTop ? 0 : frame.height / 2

            visible: screen.chosen >= 0 && screen.chosen < 6
            x: zoneX - (atLeft ? 0 : screen.hair / 2)
            y: zoneY - (atTop ? 0 : screen.hair / 2)
            width: frame.width / 3 + (atLeft ? 0 : screen.hair / 2)
                   + (atRight ? 0 : screen.hair / 2)
            height: frame.height / 2 + screen.hair / 2
            color: screen.edge
            topLeft: screen.chosen === 0
            topRight: screen.chosen === 2
            bottomLeft: screen.chosen === 3
            bottomRight: screen.chosen === 5
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
