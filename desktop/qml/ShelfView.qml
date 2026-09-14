// The shelf's first pixels: a paged grid, and only the grid.
//
// The bar, filters, search and resume strip each have rules of their own and are deliberately
// absent. This screen draws exactly what Shelf already holds. GridView owns paging through
// QAbstractItemModel's fetchMore hooks, keyboard movement, and the lifetime of cover requests.
//
// A cover lives while its delegate is visible plus one cached row. Once it leaves that buffer,
// the delegate and its Image disappear together, which lets Qt cancel the request instead of
// leaving a hand-written queue to outlive the thing it was meant to draw.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Qt5Compat.GraphicalEffects
import Leaf

Item {
    id: root

    objectName: "shelf-view"
    signal navigationBoundary(Item origin, bool forward)
    signal pointerNavigationCancelled()

    // Replaceable only so the QML can be exercised with a small model in isolation. The
    // application never assigns it: it gets the one Shelf singleton for the whole run.
    property var sourceModel: Shelf
    required property NavigationCursor navigationCursor
    readonly property int ringClearance: Theme.focusGap + 2
    // The shadow reaches 16 px left and right. Giving the viewport that full clearance keeps
    // the first and last elevations intact while the covers themselves stay on the 16 px edge.
    readonly property int viewportClearance: Widths.shelfMargin

    function isArrowKey(key) {
        return key === Qt.Key_Left || key === Qt.Key_Right
                || key === Qt.Key_Up || key === Qt.Key_Down
    }

    function isTabKey(key) {
        return key === Qt.Key_Tab || key === Qt.Key_Backtab
    }

    function enterNavigation(forward) {
        if (grid.count === 0)
            return false
        const position = forward ? 0 : grid.count - 1
        grid.keyboardIndex = position
        grid.currentIndex = position
        navigationCursor.useKeyboard(grid, position)
        grid.forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
        grid.positionViewAtIndex(position, GridView.Contain)
        return true
    }

    function enterFromFocusChain() {
        if (!grid.activeFocus || navigationCursor.region !== null)
            return

        let forward = navigationCursor.entryForward
        const previous = navigationCursor.previousFocusItem
        if (previous && grid.nextItemInFocusChain(true) === previous)
            forward = false
        else if (previous && grid.nextItemInFocusChain(false) === previous)
            forward = true

        navigationCursor.beginKeyboard(forward)
        enterNavigation(forward)
    }

    function leaveNavigation(forward) {
        const next = grid.nextItemInFocusChain(forward)
        if (!next || next === grid)
            return enterNavigation(forward)
        navigationBoundary(grid, forward)
        return true
    }

    function handleNavigationKey(key, modifiers) {
        if (!isArrowKey(key) && !isTabKey(key))
            return false
        if (grid.count === 0)
            return false
        if (grid.currentIndex < 0) {
            const backwards = key === Qt.Key_Backtab
                    || (key === Qt.Key_Tab && (modifiers & Qt.ShiftModifier))
                    || key === Qt.Key_Left || key === Qt.Key_Up
            return enterNavigation(!backwards)
        }

        let candidate = grid.currentIndex
        let forward = true
        if (key === Qt.Key_Left) {
            candidate--
            forward = false
        } else if (key === Qt.Key_Right) {
            candidate++
        } else if (key === Qt.Key_Up) {
            candidate -= grid.columns
            forward = false
        } else if (key === Qt.Key_Down) {
            candidate += grid.columns
        } else {
            const backwards = key === Qt.Key_Backtab
                    || (key === Qt.Key_Tab && (modifiers & Qt.ShiftModifier))
            candidate += backwards ? -1 : 1
            forward = !backwards
        }

        if (candidate < 0 || candidate >= grid.count)
            return leaveNavigation(forward)

        grid.forceActiveFocus(Qt.OtherFocusReason)
        grid.keyboardIndex = candidate
        grid.currentIndex = candidate
        navigationCursor.useKeyboard(grid, candidate)
        grid.positionViewAtIndex(candidate, GridView.Contain)
        return true
    }

    function followPointer(position) {
        navigationCursor.usePointer(grid, position)
        grid.currentIndex = position
        // The visual cursor stays in pointer mode, while real focus makes the next key land
        // here reliably on every window backend. Without it, X11 may send that key to the
        // window root even though the hovered tile is the navigation anchor.
        grid.forceActiveFocus(Qt.MouseFocusReason)
    }

    function releasePointer(position) {
        if (navigationCursor.mode === navigationCursor.pointerMode
                && navigationCursor.region === grid
                && navigationCursor.index === position) {
            navigationCursor.clear(grid)
            grid.currentIndex = grid.keyboardIndex
        }
    }

    function clearNavigation() {
        clearPosition()
        navigationCursor.clear(grid)
    }

    function clearPosition() {
        grid.keyboardIndex = -1
        grid.currentIndex = -1
    }

    Component.onCompleted: sourceModel.reload()

    Connections {
        target: root.navigationCursor

        function onNavigationKeyRequested(owner, key, modifiers) {
            if (owner === grid)
                root.handleNavigationKey(key, modifiers)
        }

        function onResetRequested(owner) {
            if (owner === grid)
                root.clearPosition()
        }
    }

    GridView {
        id: grid

        objectName: "shelf-grid"
        property int columns: Widths.shelfColumns
        property int keyboardIndex: -1
        readonly property real coverWidth: cellWidth - Widths.shelfGap
        readonly property real coverHeight: coverWidth * 1.5

        anchors.fill: parent

        model: root.sourceModel
        // Subtract both cover clearances from the viewport before sharing the useful width.
        // That keeps the first and last covers on the same 16 px edges in both palettes.
        cellWidth: (width - 2 * root.viewportClearance + Widths.shelfGap)
                   / Math.max(1, columns)
        // 40 px is the title's two 20 px lines; one-line names leave breathing room before
        // the next row rather than moving that row upward.
        cellHeight: Math.ceil(root.ringClearance + coverHeight + 6 + 40 + 2 + 18
                              + Widths.shelfGap)

        cacheBuffer: cellHeight
        currentIndex: -1
        keyNavigationEnabled: true
        keyNavigationWraps: false
        activeFocusOnTab: count > 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        onActiveFocusChanged: {
            if (activeFocus && root.navigationCursor.region === null)
                root.enterFromFocusChain()
        }
        Keys.onPressed: event => event.accepted = root.handleNavigationKey(
            event.key, event.modifiers)

        // GridView owns the pointer grab over its viewport, including its bare paper. Clear
        // through the page-wide cursor here, then let Main return focus to the page root.
        PointHandler {
            target: null
            onActiveChanged: {
                if (!active)
                    return
                root.clearNavigation()
                root.pointerNavigationCancelled()
            }
        }

        delegate: Item {
            id: tile

            required property int index
            required property string seriesId
            required property string name
            required property string work
            required property string cover
            required property string medium
            required property string volumes
            required property bool inProgress

            objectName: "tile-" + seriesId
            width: grid.cellWidth
            height: grid.cellHeight

            Accessible.role: Accessible.ListItem
            Accessible.name: name
            Accessible.description: volumes
            Accessible.focusable: true
            Accessible.focused: root.navigationCursor.mode
                                === root.navigationCursor.keyboardMode
                                && root.navigationCursor.region === grid
                                && root.navigationCursor.index === tile.index

            MouseArea {
                id: tileHover

                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                hoverEnabled: true
                // Crossing into a tile hands emphasis to the pointer. Motion inside the
                // same tile is deliberately ignored: a stationary hover must not reclaim
                // the cursor just after a keyboard event selected somewhere else.
                onEntered: root.followPointer(tile.index)
                onExited: root.releasePointer(tile.index)
            }

            Item {
                id: coverFrame

                objectName: "cover-frame-" + tile.seriesId
                x: root.viewportClearance
                y: root.viewportClearance
                width: grid.coverWidth
                height: grid.coverHeight

                CoverShadow {
                    seriesId: tile.seriesId
                }

                Rectangle {
                    id: clippedCover

                    objectName: "clipped-cover-" + tile.seriesId
                    anchors.fill: parent
                    radius: Theme.coverRadius
                    color: Theme.onPaper
                    clip: true
                    antialiasing: true
                    Item {
                        id: coverSource
                        anchors.fill: parent

                        Image {
                            id: coverImage

                            objectName: "cover-" + tile.seriesId
                            anchors.fill: parent
                            source: tile.cover
                            asynchronous: true
                            cache: true
                            fillMode: Image.PreserveAspectCrop
                        }

                        Rectangle {
                            objectName: "in-progress-" + tile.seriesId
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 4
                            color: Theme.emerald
                            opacity: tile.inProgress ? 1 : 0
                        }
                    }

                    ShaderEffectSource {
                        id: coverTexture
                        sourceItem: coverSource
                        hideSource: true
                        visible: false
                    }

                    OpacityMask {
                        anchors.fill: parent
                        source: coverTexture
                        maskSource: Rectangle {
                            width: clippedCover.width
                            height: clippedCover.height
                            radius: clippedCover.radius
                            antialiasing: true
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.coverRadius
                    color: "transparent"
                    border.color: Theme.rule
                    border.width: 1
                    antialiasing: true
                }

                FocusRing {
                    objectName: "focus-" + tile.seriesId
                    visible: root.navigationCursor.region === grid
                             && root.navigationCursor.index === tile.index
                }
            }

            Text {
                id: title

                objectName: "title-" + tile.seriesId
                anchors.left: coverFrame.left
                anchors.right: coverFrame.right
                anchors.top: coverFrame.bottom
                anchors.topMargin: 6
                height: Math.min(implicitHeight, 40)
                text: tile.name
                color: Theme.ink
                font.family: Theme.displayFamily
                font.pixelSize: 16
                font.weight: Font.DemiBold
                lineHeightMode: Text.FixedHeight
                lineHeight: 20
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Text {
                objectName: "volumes-" + tile.seriesId
                anchors.left: coverFrame.left
                anchors.right: coverFrame.right
                anchors.top: title.bottom
                anchors.topMargin: 2
                height: 18
                text: tile.volumes
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: 14
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }

    BusyIndicator {
        objectName: "shelf-first-load"
        anchors.centerIn: parent
        running: root.sourceModel.loading && grid.count === 0
        visible: running
        palette.highlight: Theme.emerald
    }

    BusyIndicator {
        objectName: "shelf-next-page"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Widths.shelfMargin
        running: root.sourceModel.loading && grid.count > 0
        visible: running
        palette.highlight: Theme.emerald
    }

    Rectangle {
        id: trouble

        objectName: "shelf-trouble"
        z: 2
        width: Math.max(0, Math.min(560, root.width - 2 * Widths.shelfMargin))
        height: troubleText.implicitHeight + 32
        x: Math.round((root.width - width) / 2)
        y: grid.count === 0 ? Math.round((root.height - height) / 2) : root.height - height - Widths.shelfMargin
        radius: Theme.cardRadius
        color: Theme.surface
        visible: troubleText.text.length > 0

        Text {
            id: troubleText

            objectName: "shelf-trouble-text"
            anchors.fill: parent
            anchors.margins: 16
            text: root.sourceModel.trouble
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 15
            lineHeightMode: Text.FixedHeight
            lineHeight: 22
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
