// A scope-changing action. It navigates within the search; it never unfolds content inline.

import QtQuick
import Leaf

Rectangle {
    id: link

    required property string label
    required property NavigationCursor navigationCursor

    signal triggered()
    signal wentPast(bool forward)

    objectName: "search-link"
    height: 38
    radius: Theme.buttonRadius
    color: activeFocus || pointer.hovered ? Theme.onPaper : "transparent"
    activeFocusOnTab: false

    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.focusable: true
    Accessible.focused: activeFocus
    Accessible.onPressAction: link.triggered()

    function takeFocus(forward) {
        navigationCursor.useKeyboard(link, 0)
        forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
        return true
    }

    function move(key, modifiers) {
        const back = key === Qt.Key_Left || key === Qt.Key_Up
                || key === Qt.Key_Backtab
                || (key === Qt.Key_Tab && (modifiers & Qt.ShiftModifier))
        const on = key === Qt.Key_Right || key === Qt.Key_Down || key === Qt.Key_Tab
        if (!back && !on)
            return false
        navigationCursor.clear(link)
        wentPast(on)
        return true
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            triggered()
            event.accepted = true
        } else {
            event.accepted = move(event.key, event.modifiers)
        }
    }

    Connections {
        target: link.navigationCursor

        function onNavigationKeyRequested(owner, key, modifiers) {
            if (owner === link)
                link.move(key, modifiers)
        }

        function onResetRequested(owner) {
            if (owner === link)
                link.navigationCursor.clear(link)
        }
    }

    HoverHandler {
        id: pointer
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: {
            link.navigationCursor.usePointer(link, 0)
            link.forceActiveFocus(Qt.MouseFocusReason)
            link.triggered()
        }
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: link.label
        color: Theme.emerald
        font.family: Theme.textFamily
        font.pixelSize: 14
        font.weight: Font.Medium
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        text: "›"
        color: Theme.emerald
        font.family: Theme.textFamily
        font.pixelSize: 21
    }

    FocusRing {
        cornerRadius: link.radius
        visible: link.activeFocus
                 && link.navigationCursor.mode === link.navigationCursor.keyboardMode
                 && link.navigationCursor.region === link
    }
}
