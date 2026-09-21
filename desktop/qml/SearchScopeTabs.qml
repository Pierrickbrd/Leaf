// The three permanent search scopes: overview, series and files with their exact counts.

import QtQuick
import Leaf

Item {
    id: tabs

    required property NavigationCursor navigationCursor
    property int currentIndex: 0

    signal selected(int index)
    signal wentPast(bool forward)

    objectName: "search-scope-tabs"
    height: 52

    readonly property var choices: [
        { "name": "overview", "label": Search.overviewLabel },
        { "name": "series", "label": Search.seriesHeading },
        { "name": "files", "label": Search.filesHeading }
    ]

    function tabAt(position) {
        return tabRepeater.itemAt(position)
    }

    function takeFocus(forward) {
        const position = forward ? 0 : choices.length - 1
        const target = tabAt(position)
        if (!target)
            return false
        navigationCursor.useKeyboard(tabs, position)
        target.forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
        return true
    }

    function move(key, modifiers) {
        let here = navigationCursor.region === tabs ? navigationCursor.index : currentIndex
        const back = key === Qt.Key_Left || key === Qt.Key_Up
                || key === Qt.Key_Backtab
                || (key === Qt.Key_Tab && (modifiers & Qt.ShiftModifier))
        const on = key === Qt.Key_Right || key === Qt.Key_Down || key === Qt.Key_Tab
        if (!back && !on)
            return false

        const leaves = key === Qt.Key_Up || key === Qt.Key_Down
        const next = here + (on ? 1 : -1)
        if (leaves || next < 0 || next >= choices.length) {
            navigationCursor.clear(tabs)
            wentPast(on)
            return true
        }
        navigationCursor.useKeyboard(tabs, next)
        tabAt(next).forceActiveFocus(on ? Qt.TabFocusReason : Qt.BacktabFocusReason)
        return true
    }

    Keys.onPressed: event => event.accepted = move(event.key, event.modifiers)

    Connections {
        target: tabs.navigationCursor

        function onNavigationKeyRequested(owner, key, modifiers) {
            if (owner === tabs)
                tabs.move(key, modifiers)
        }

        function onResetRequested(owner) {
            if (owner === tabs)
                tabs.navigationCursor.clear(tabs)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.paper
    }

    Row {
        id: buttons

        x: Widths.shelfMargin
        y: 9
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

                objectName: "search-tab-" + modelData.name
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

                onActiveFocusChanged: {
                    if (activeFocus)
                        tabs.navigationCursor.useKeyboard(tabs, index)
                }

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
                    onTapped: {
                        tab.forceActiveFocus(Qt.MouseFocusReason)
                        tabs.navigationCursor.usePointer(tabs, index)
                        tabs.selected(index)
                    }
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

                FocusRing {
                    objectName: tab.objectName + "-focus"
                    cornerRadius: tab.radius
                    visible: tab.activeFocus
                             && tabs.navigationCursor.mode
                                === tabs.navigationCursor.keyboardMode
                             && tabs.navigationCursor.region === tabs
                }
            }
        }
    }
}
