// Search files in a four-line preview or in their own independently scrolled view.

import QtQuick
import QtQuick.Controls
import Leaf

ListView {
    id: list

    property var sourceModel: Search
    required property NavigationCursor navigationCursor
    property bool preview: false
    property string viewName: "search-files"

    signal wentPast(bool forward)
    signal blankPressed()

    objectName: viewName
    model: sourceModel
    clip: true
    interactive: !preview
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationEnabled: false
    keyNavigationWraps: false
    activeFocusOnTab: false
    currentIndex: -1
    spacing: 2
    cacheBuffer: preview ? 0 : 144

    readonly property int previewMaximum: 4
    readonly property int navigableCount: preview
                                          ? Math.min(count, previewMaximum) : count
    readonly property real rowHeight: 72

    function enter(position, reason) {
        if (position < 0 || position >= navigableCount)
            return false
        currentIndex = position
        navigationCursor.useKeyboard(list, position)
        forceActiveFocus(reason)
        positionViewAtIndex(position, ListView.Contain)
        return true
    }

    function takeFocus(forward) {
        return enter(forward ? 0 : navigableCount - 1,
                     forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
    }

    function move(key, modifiers) {
        const backwards = key === Qt.Key_Left || key === Qt.Key_Up
                || key === Qt.Key_Backtab
                || (key === Qt.Key_Tab && (modifiers & Qt.ShiftModifier))
        const forwards = key === Qt.Key_Right || key === Qt.Key_Down
                || key === Qt.Key_Tab
        if (!backwards && !forwards)
            return false
        if (navigableCount === 0)
            return false

        const here = navigationCursor.region === list ? navigationCursor.index : currentIndex
        const next = here + (backwards ? -1 : 1)
        if (next < 0 || next >= navigableCount) {
            // At the loaded end of the full list, ask the model for its next page. The focus
            // stays on the last honest row; the next key moves once the rows have arrived.
            if (!backwards && !preview && sourceModel.remaining > 0) {
                sourceModel.expand()
                return true
            }
            navigationCursor.clear(list)
            currentIndex = -1
            wentPast(!backwards)
            return true
        }
        return enter(next, Qt.OtherFocusReason)
    }

    function pointAt(position) {
        navigationCursor.usePointer(list, position)
        currentIndex = position
    }

    function stopPointingAt(position) {
        if (navigationCursor.mode === navigationCursor.pointerMode
                && navigationCursor.region === list
                && navigationCursor.index === position) {
            navigationCursor.clear(list)
            currentIndex = -1
        }
    }

    function resetPosition() {
        currentIndex = -1
        navigationCursor.clear(list)
    }

    Keys.onPressed: event => {
        // The line the keys are on, opened: the row holds no focus of its own to hear it.
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter
             || event.key === Qt.Key_Space) && currentItem && currentIndex < navigableCount) {
            currentItem.activated()
            event.accepted = true
            return
        }
        event.accepted = move(event.key, event.modifiers)
    }

    Connections {
        target: list.navigationCursor

        function onNavigationKeyRequested(owner, key, modifiers) {
            if (owner === list)
                list.move(key, modifiers)
        }

        function onResetRequested(owner) {
            if (owner === list)
                list.resetPosition()
        }
    }

    onCountChanged: {
        if (currentIndex >= navigableCount)
            resetPosition()
    }

    PointHandler {
        target: null
        onActiveChanged: {
            if (!active)
                return
            list.resetPosition()
            list.blankPressed()
        }
    }

    delegate: FileResultRow {
        width: list.width
        selected: list.navigationCursor.region === list
                  && list.navigationCursor.index === index
        onPointerEntered: list.pointAt(index)
        onPointerExited: list.stopPointingAt(index)
        // A file found is a file to read: a click opens it in the reader, at the page where
        // reading stopped, as a line of a series page does. It only selected the line — the
        // reader did not exist yet — and Enter on it did nothing at all.
        onActivated: Navigation.open(Navigation.Reader, { "series": seriesId, "entry": entryId })
    }

    footer: Item {
        width: list.width
        height: Search.loading && !list.preview ? 46 : 0

        BusyIndicator {
            anchors.centerIn: parent
            width: 30
            height: 30
            running: parent.height > 0
            visible: running
            palette.highlight: Theme.emerald
        }
    }
}
