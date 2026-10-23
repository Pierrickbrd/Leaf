// One setting with a short list of answers, each a pill — words, with a picto or a swatch
// when the answer has one.
//
// Pills and not a dropdown: three answers fit on a line, and a menu that has to be opened
// to learn what it holds hides three words behind two clicks. The same shape as the tabs
// above them, for the same reason — a screen with one way of choosing is learnt once.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Item {
    id: choice

    /// `[{ value, label, icon, name }]`, worded and ordered in C++. `icon` may be left out:
    /// the reader's card is words alone, because a picto beside « De droite à gauche » says it
    /// twice and its rows are named beside their pills. `name` names a pill among the items
    /// when it has no picto to be named by; without either, its value does.
    required property var options
    required property int chosen
    /// Whether it follows the window (`Widths.scale`). Not in the Settings screen yet; the
    /// reader's card asks for it, or its pills stayed at their drawn size inside a card that
    /// had grown round them (owner's test of 22/10/2026).
    property bool scaled: false
    /// A colour per value, drawn as a small square before the word: the reader's backdrops,
    /// where the colour is the answer and a word alone makes the reader picture it. Empty for
    /// every other choice.
    property var swatches: ({})
    /// How wide its pills are on one line, wrapping aside: what a row beside a name needs to
    /// stand without wrapping, so that the card can decide when to put the name above instead.
    readonly property real lineWidth: {
        let wide = 0
        let count = 0
        // Indexed, not `for…of`: Qt 6.4's list of children is not iterable, and the loop
        // threw a TypeError every time the card was drawn.
        for (let at = 0; at < row.children.length; ++at) {
            const one = row.children[at]
            if (one.isPill === true) {
                wide += one.width
                ++count
            }
        }
        return count > 0 ? wide + (count - 1) * row.spacing : 0
    }

    function sized(drawn: real): int {
        return choice.scaled ? Widths.scale.size(drawn) : drawn
    }

    signal picked(int value)

    // No heading of its own: the card above already names the setting, and saying it twice
    // is the kind of thing a screen does when nobody reads it back.
    implicitHeight: row.height
    height: implicitHeight

    // A flow and not a row: four directions in words are wider than a narrow window's card,
    // and a row that is wider than its card runs out of it. Three pills still make one line.
    Flow {
        id: row

        width: choice.width
        spacing: choice.sized(8)

        Repeater {
            model: choice.options

            delegate: Rectangle {
                id: pill

                required property var modelData

                readonly property bool isPill: true
                readonly property bool lit: choice.chosen === modelData.value
                readonly property bool hovered: pointer.hovered

                // Pictured, swatched, or words alone; named by its name, else the picto, else
                // the value.
                readonly property bool pictured: modelData.icon !== undefined
                                                 && modelData.icon !== ""
                readonly property var shade: choice.swatches[modelData.value]
                readonly property bool swatched: shade !== undefined

                objectName: choice.objectName + "-"
                            + (modelData.name !== undefined ? modelData.name
                                                            : pictured ? modelData.icon
                                                                       : modelData.value)
                width: word.implicitWidth + choice.sized(30)
                       + (pictured ? glyph.width
                                   : swatched ? swatch.width + choice.sized(5) : 0)
                height: choice.sized(36)
                radius: height / 2
                color: lit ? Theme.emeraldWash
                           : (hovered || activeFocus ? Theme.onPaper : "transparent")
                border.color: lit ? Theme.emerald : Theme.rule
                border.width: 1
                antialiasing: true
                activeFocusOnTab: false

                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.label
                Accessible.checkable: true
                Accessible.checked: lit
                Accessible.onPressAction: choice.picked(modelData.value)

                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                            || event.key === Qt.Key_Enter) {
                        choice.picked(pill.modelData.value)
                        event.accepted = true
                    }
                }

                HoverHandler {
                    id: pointer

                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: choice.picked(pill.modelData.value)
                }

                FocusRing {
                    objectName: pill.objectName + "-focus"
                    cornerRadius: pill.height / 2
                }

                Glyph {
                    id: glyph

                    x: choice.sized(12)
                    anchors.verticalCenter: parent.verticalCenter
                    side: choice.sized(18)
                    visible: pill.pictured
                    source: pill.pictured ? "assets/icons/" + pill.modelData.icon + ".svg" : ""
                    tint: pill.lit ? Theme.emerald : Theme.inkSoft
                }

                // The paper itself, small, with an edge: black on a dark card and white on a
                // light one would otherwise be a gap where a square should be.
                Rectangle {
                    id: swatch

                    objectName: pill.objectName + "-swatch"
                    x: choice.sized(12)
                    anchors.verticalCenter: parent.verticalCenter
                    width: choice.sized(14)
                    height: width
                    radius: choice.sized(4)
                    visible: pill.swatched
                    color: pill.swatched ? pill.shade : "transparent"
                    border.color: Theme.rule
                    border.width: 1
                    antialiasing: true
                }

                Text {
                    id: word

                    objectName: pill.objectName + "-text"
                    x: pill.pictured ? glyph.x + glyph.width + choice.sized(8)
                                     : pill.swatched ? swatch.x + swatch.width + choice.sized(8)
                                                     : choice.sized(15)
                    anchors.verticalCenter: parent.verticalCenter
                    text: pill.modelData.label
                    // Plain, never rich: a name typed on another device — « <b>x</b> » — is shown as
                    // typed, not drawn as markup.
                    textFormat: Text.PlainText
                    color: pill.lit ? Theme.emerald : Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: choice.scaled ? Widths.scale.body(14) : 14
                    font.weight: pill.lit ? Font.Medium : Font.Normal
                }
            }
        }
    }
}
