// The sections of the settings, as the same pills the search scopes use.
//
// The same shape and not a new one: a screen with one grammar of choosing between views is
// a screen a reader learns once. What differs is what they hold, which is the only thing
// that should differ.

import QtQuick
import Leaf

Item {
    id: tabs

    /// `[{ name, label }]`, worded in C++ like everything else a reader sees.
    required property var choices
    property int currentIndex: 0

    signal selected(int index)
    /// The way out of this page. It sits with the tabs and not in the application bar: a
    /// control that leaves a page belongs to that page, and the bar's row is the shelf's.
    signal wentBack()

    objectName: "settings-tabs"
    height: 56

    function tabAt(position) {
        return tabRepeater.itemAt(position)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.paper
    }

    BarButton {
        id: back

        objectName: "back-button"
        x: Widths.shelfMargin
        anchors.verticalCenter: parent.verticalCenter
        visible: Navigation.canGoBack
        source: "assets/icons/arrow_back.svg"
        label: Navigation.backLabel
        onTriggered: tabs.wentBack()
    }

    Row {
        x: back.visible ? back.x + back.width + 14 : Widths.shelfMargin
        anchors.verticalCenter: parent.verticalCenter
        height: 34
        spacing: 6

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
                activeFocusOnTab: false

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
                    text: tab.modelData.label
                    color: tab.chosen ? Theme.emerald : Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 14
                    font.weight: tab.chosen ? Font.Medium : Font.Normal
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.rule
        opacity: Theme.dark ? 0.7 : 0.45
    }
}
