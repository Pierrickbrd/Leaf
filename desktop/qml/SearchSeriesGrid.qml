// The series part of a search, either one preview row or a full independently scrolled grid.

import QtQuick
import Leaf

GridView {
    id: grid

    property var sourceModel: Shelf
    required property NavigationCursor navigationCursor
    property bool preview: false
    property string viewName: "search-series-grid"

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

    property int columns: Widths.shelfColumns
    property int keyboardIndex: -1
    readonly property int viewportClearance: Widths.shelfMargin
    readonly property real coverWidth: cellWidth - Widths.shelfGap
    readonly property real coverHeight: coverWidth * 1.5
    readonly property int navigableCount: preview ? Math.min(count, columns) : count
    readonly property int previewCount: Math.min(count, columns)

    cellWidth: (width - 2 * viewportClearance + Widths.shelfGap)
               / Math.max(1, columns)
    cellHeight: Math.ceil(Theme.focusGap + 2 + coverHeight + 6 + 40 + 2 + 18
                          + Widths.shelfGap)
    cacheBuffer: preview ? 0 : cellHeight

    function enter(position, reason) {
        if (position < 0 || position >= navigableCount)
            return false
        keyboardIndex = position
        currentIndex = position
        navigationCursor.useKeyboard(grid, position)
        forceActiveFocus(reason)
        positionViewAtIndex(position, GridView.Contain)
        return true
    }

    function takeFocus(forward) {
        return enter(forward ? 0 : navigableCount - 1,
                     forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
    }

    function move(key, modifiers) {
        if (navigableCount === 0)
            return false

        const backwards = key === Qt.Key_Left || key === Qt.Key_Up
                || key === Qt.Key_Backtab
                || (key === Qt.Key_Tab && (modifiers & Qt.ShiftModifier))
        const forwards = key === Qt.Key_Right || key === Qt.Key_Down
                || key === Qt.Key_Tab
        if (!backwards && !forwards)
            return false

        if (keyboardIndex < 0)
            return takeFocus(!backwards)

        let candidate = keyboardIndex
        if (key === Qt.Key_Left)
            candidate -= 1
        else if (key === Qt.Key_Right)
            candidate += 1
        else if (key === Qt.Key_Up)
            candidate -= columns
        else if (key === Qt.Key_Down)
            candidate += columns
        else
            candidate += backwards ? -1 : 1

        if (candidate < 0 || candidate >= navigableCount) {
            navigationCursor.clear(grid)
            keyboardIndex = -1
            currentIndex = -1
            wentPast(!backwards)
            return true
        }
        return enter(candidate, Qt.OtherFocusReason)
    }

    function pointAt(position) {
        navigationCursor.usePointer(grid, position)
        currentIndex = position
    }

    function stopPointingAt(position) {
        if (navigationCursor.mode === navigationCursor.pointerMode
                && navigationCursor.region === grid
                && navigationCursor.index === position) {
            navigationCursor.clear(grid)
            currentIndex = keyboardIndex
        }
    }

    function resetPosition() {
        keyboardIndex = -1
        currentIndex = -1
        navigationCursor.clear(grid)
    }

    Keys.onPressed: event => event.accepted = move(event.key, event.modifiers)

    Connections {
        target: grid.navigationCursor

        function onNavigationKeyRequested(owner, key, modifiers) {
            if (owner === grid)
                grid.move(key, modifiers)
        }

        function onResetRequested(owner) {
            if (owner === grid)
                grid.resetPosition()
        }
    }

    onCountChanged: {
        if (keyboardIndex >= navigableCount)
            resetPosition()
    }

    PointHandler {
        target: null
        onActiveChanged: {
            if (!active)
                return
            grid.resetPosition()
            grid.blankPressed()
        }
    }

    delegate: SeriesTile {
        cellWidth: grid.cellWidth
        cellHeight: grid.cellHeight
        coverWidth: grid.coverWidth
        coverHeight: grid.coverHeight
        viewportClearance: grid.viewportClearance
        selected: grid.navigationCursor.region === grid
                  && grid.navigationCursor.index === index
        onPointerEntered: grid.pointAt(index)
        onPointerExited: grid.stopPointingAt(index)
        onOpened: Navigation.open(Navigation.Series, { "series": seriesId })
    }
}
