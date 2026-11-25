// The sections of the settings, as the same pills the search scopes use.
//
// The same shape and not a new one: a screen with one grammar of choosing between views is
// a screen a reader learns once. What differs is what they hold, which is the only thing
// that should differ — and so they stand on the same `PageStrip`: centred in a band of 56,
// these stood 11 pixels under the bar where the search's stood 9 and the shelf's band 16.
// And with no rule under them, as the search's have none: the one strip with a rule made two
// pages two grammars. The way back leads them, as it leads the series page's strip.

import QtQuick
import Leaf

PageStrip {
    id: tabs

    /// `[{ name, label }]`, worded in C++ like everything else a reader sees.
    required property var choices
    property int currentIndex: 0

    signal selected(int index)

    objectName: "settings-tabs"

    function tabAt(position) {
        return tabRepeater.itemAt(position)
    }

    /// Where the screen begins for the keys: the way back when there is one, else the first tab.
    function firstStop() {
        return back.visible ? back : tabRepeater.itemAt(0)
    }

    BackButton {
        id: back

        // Qt's own Tab order walks this page, as it walks the tabs after it.
        activeFocusOnTab: visible
    }

    Row {
        id: tabRow

        height: 34
        spacing: Widths.space.pill

        Repeater {
            id: tabRepeater

            model: tabs.choices

            delegate: Rectangle {
                id: tab

                required property int index
                required property var modelData

                readonly property bool chosen: tabs.currentIndex === index
                readonly property bool hovered: pointer.hovered

                objectName: "settings-tab-" + modelData.name
                width: word.implicitWidth + 26
                height: 34
                radius: height / 2
                color: chosen ? Theme.emeraldWash
                              : (hovered || activeFocus ? Theme.onPaper : "transparent")
                border.color: chosen ? Theme.emerald : "transparent"
                border.width: 1
                // Stops, each of them: none was, and the keys could reach no tab but the first
                // and nothing under it.
                activeFocusOnTab: true

                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.label
                Accessible.selected: chosen
                Accessible.focusable: true
                Accessible.focused: activeFocus
                Accessible.onPressAction: tabs.selected(index)

                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                            || event.key === Qt.Key_Enter) {
                        tabs.selected(index)
                        event.accepted = true
                    }
                }

                HoverHandler {
                    id: pointer

                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: tabs.selected(tab.index)
                }

                FocusRing {
                    objectName: tab.objectName + "-focus"
                    cornerRadius: tab.height / 2
                }

                Text {
                    id: word

                    objectName: tab.objectName + "-text"
                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: tab.modelData.label
                    color: tab.chosen ? Theme.emerald : Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 14
                    font.weight: tab.chosen ? Font.Medium : Font.Normal
                }
            }
        }
    }
}
