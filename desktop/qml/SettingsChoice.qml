// One setting with a short list of answers, each a pill with its own picto.
//
// Pills and not a dropdown: three answers fit on a line, and a menu that has to be opened
// to learn what it holds hides three words behind two clicks. The same shape as the tabs
// above them, for the same reason — a screen with one way of choosing is learnt once.

import QtQuick
import Qt5Compat.GraphicalEffects
import Leaf

Item {
    id: choice

    /// `[{ value, label, icon }]`, worded and ordered in C++.
    required property var options
    required property int chosen

    signal picked(int value)

    // No heading of its own: the card above already names the setting, and saying it twice
    // is the kind of thing a screen does when nobody reads it back.
    implicitHeight: row.height
    height: implicitHeight

    Row {
        id: row

        spacing: 8

        Repeater {
            model: choice.options

            delegate: Rectangle {
                id: pill

                required property var modelData

                readonly property bool lit: choice.chosen === modelData.value
                readonly property bool hovered: pointer.hovered

                objectName: choice.objectName + "-" + modelData.icon
                width: word.implicitWidth + glyph.width + 30
                height: 36
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
                    onTapped: choice.picked(pill.modelData.value)
                }

                FocusRing {
                    objectName: pill.objectName + "-focus"
                    cornerRadius: pill.height / 2
                }

                Image {
                    id: glyph

                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 18
                    source: "assets/icons/" + pill.modelData.icon + ".svg"
                    sourceSize: Qt.size(18, 18)
                    visible: false
                }

                // The glyphs ship without a fill, so the client tints them — see the note
                // beside them. One file serves both palettes and both states.
                ColorOverlay {
                    anchors.fill: glyph
                    source: glyph
                    color: pill.lit ? Theme.emerald : Theme.inkSoft
                }

                Text {
                    id: word

                    objectName: pill.objectName + "-text"
                    anchors.left: glyph.right
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: pill.modelData.label
                    color: pill.lit ? Theme.emerald : Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 14
                    font.weight: pill.lit ? Font.Medium : Font.Normal
                }
            }
        }
    }
}
