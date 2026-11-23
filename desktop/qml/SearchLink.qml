// A scope-changing action. It navigates within the search; it never unfolds content inline.
//
// As wide as its words, the chevron right after them. It took the width of what it followed:
// the series' link ran the window's whole width and the files' the column's, so the two
// chevrons stood at two places, both far from the words they belonged to, and the ring of the
// series' one ran under the last title of the row above it (audit, 25/10/2026).

import QtQuick
import Leaf

Rectangle {
    id: link

    required property string label
    required property NavigationCursor navigationCursor

    signal triggered()
    signal wentPast(bool forward)

    objectName: "search-link"
    implicitWidth: Widths.space.compact + words.implicitWidth + Widths.space.label
                   + chevron.implicitWidth + Widths.space.compact
    // Never past the column it stands in: the words give way first.
    width: parent ? Math.min(implicitWidth, parent.width) : implicitWidth
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
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: {
            link.navigationCursor.usePointer(link, 0)
            link.forceActiveFocus(Qt.MouseFocusReason)
            link.triggered()
        }
    }

    Text {
        id: words

        x: Widths.space.compact
        width: Math.min(implicitWidth, Math.max(0, link.width - 2 * Widths.space.compact
                                                    - Widths.space.label - chevron.implicitWidth))
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        textFormat: Text.PlainText
        text: link.label
        color: Theme.emerald
        font.family: Theme.textFamily
        font.pixelSize: 14
        font.weight: Font.Medium
    }

    Text {
        id: chevron

        x: words.x + words.width + Widths.space.label
        anchors.verticalCenter: parent.verticalCenter
        textFormat: Text.PlainText
        text: "›"
        color: Theme.emerald
        font.family: Theme.textFamily
        font.pixelSize: 21
    }

    FocusRing {
        cornerRadius: link.radius
        focused: link.activeFocus
                 && link.navigationCursor.mode === link.navigationCursor.keyboardMode
                 && link.navigationCursor.region === link
    }
}
