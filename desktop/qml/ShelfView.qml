// The shelf: search rows, the resume band, filters, and a paged grid.
//
// The application bar lives in Main because later screens share it. This screen draws what
// Shelf, Search and Resume already hold. GridView owns paging through
// QAbstractItemModel's fetchMore hooks, keyboard movement, and the lifetime of cover requests.
//
// The band leaves with the scroll while the row above it stays — the artifact is explicit
// about both, in one line each. The band is therefore the view's **header**, which is what a
// header is: content that scrolls with the rest. A `topMargin` was the other way and it does
// not work — a GridView clamps its position back out of its own top margin, whatever sets it,
// so the band came up cut by exactly the room it had been given.
//
// That header is why this file carries no `pragma ComponentBehavior: Bound`: under Qt 6.4.2 a
// header component in a file that has it is never Ready, `headerItem` stays null for good, and
// the only trace is one line on stderr while everything else works.
//
// A cover lives while its delegate is visible plus one cached row. Once it leaves that buffer,
// the delegate and its Image disappear together, which lets Qt cancel the request instead of
// leaving a hand-written queue to outlive the thing it was meant to draw.

import QtQuick
import QtQuick.Controls
import Qt5Compat.GraphicalEffects
import Leaf

Item {
    id: root

    objectName: "shelf-view"
    // The band slides out through the top of this item. Without the clip it would go on
    // drawing over whatever the shell puts above the screen — the bar, when it exists.
    clip: true
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
        if (Search.active)
            return searchWorkspace.takeFocus(forward)
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

    /// The band's button, when there is one to focus: the band is absent whenever nothing is
    /// started, and the row and the covers then close over its place in the order.
    function hasBandButton() {
        return grid.headerItem && grid.headerItem.button && grid.headerItem.button.visible
    }

    function focusTheBand() {
        if (!hasBandButton())
            return false
        grid.headerItem.button.forceActiveFocus(Qt.TabFocusReason)
        return true
    }

    /// Takes the focus by the first thing drawn on this screen going forward — the band, then
    /// the row, then the covers, whichever of them are there —
    /// and by the last cover coming back. False when there is nothing at all to focus, which
    /// is what sends the focus straight round to the other end of the bar.
    function takeFocus(forward) {
        if (Search.active)
            return searchWorkspace.takeFocus(forward)
        if (!forward)
            return enterNavigation(false)
        if (focusTheBand())
            return true
        if (filterPills.takeFocus(true))
            return true
        return enterNavigation(true)
    }

    /// Whether the focus came from something drawn above the grid — the band's button, a
    /// filter chip. Asked by identity and not of the focus chain, because the chain wraps:
    /// with only these stops on the screen, what is above the grid is also what follows it.
    function above(item) {
        let candidate = item
        while (candidate) {
            if (candidate === grid.headerItem || candidate === filterPills)
                return true
            candidate = candidate.parent
        }
        return false
    }

    function enterFromFocusChain() {
        if (!grid.activeFocus)
            return
        // A highlight the pointer is painting does not stop the keyboard from entering: the
        // focus has just arrived here, and the focus is what the keys follow. Only a keyboard
        // cursor already owning this region means there is nothing to enter.
        if (navigationCursor.region !== null
                && navigationCursor.mode === navigationCursor.keyboardMode)
            return

        let forward = navigationCursor.entryForward
        const previous = navigationCursor.previousFocusItem
        // Which end to enter by, read from where the focus came from.
        //
        // The band is named outright rather than found by identity in the chain, because the
        // chain *wraps*: with the button the only other stop on this screen, it is both the
        // item after the grid and the item before it. The test below matched on the first and
        // decided the focus was coming backwards, so pressing Down on the button entered the
        // grid by its **last** cover. The band is above the grid; coming from it is coming
        // forward, and no amount of asking the chain will say so.
        if (previous && above(previous))
            forward = true
        else if (previous && grid.nextItemInFocusChain(true) === previous)
            forward = false
        else if (previous && grid.nextItemInFocusChain(false) === previous)
            forward = true

        // Coming in forward, the first stop is the band's button — it is drawn above the
        // covers, and the focus chain cannot say so: the band is the view's header, and a
        // child comes after its parent there, so Tab reaches the grid before the button
        // inside it. The grid owns the movement inside itself, band included.
        if (forward && !above(previous)) {
            if (focusTheBand() || filterPills.takeFocus(true))
                return
        }

        navigationCursor.beginKeyboard(forward)
        enterNavigation(forward)
    }

    function leaveNavigation(forward) {
        // Upwards, the band. Named rather than looked up in the focus chain, because the band
        // is the view's header and a child comes *after* its parent there: walking backwards
        // from the grid leaves the screen altogether instead of landing on the button just
        // above the first cover. The cursor is cleared by hand for the same reason — the
        // button belongs to the grid now, so nothing else notices the focus leaving the rows.
        if (!forward && (filterPills.stops().length > 0 || hasBandButton())) {
            navigationCursor.clear(grid)
            clearPosition()
            if (!filterPills.takeFocus(false))
                focusTheBand()
            return true
        }

        // Past either end of this screen, the page. Not asked of the focus chain any more:
        // with the bar's stops, the chips and the band's button all driven by hand rather than
        // traversed, that chain holds little more than the grid itself — and it answered "the
        // grid" to what comes after the last cover, which sent the focus back into the covers
        // it had just left instead of round to the bar.
        navigationCursor.clear(grid)
        clearPosition()
        navigationBoundary(grid, forward)
        return true
    }

    function handleNavigationKey(key, modifiers) {
        if (!isArrowKey(key) && !isTabKey(key))
            return false
        grid.followed = true
        if (grid.count === 0)
            return false
        // From where the *keyboard* stands, and never from what the pointer is over. Hovering
        // writes `currentIndex` so that the cover under the pointer is the one highlighted;
        // starting a key from it meant an arrow continued from wherever the mouse had been
        // left, which is not where the reader is looking.
        if (grid.keyboardIndex < 0) {
            const backwards = key === Qt.Key_Backtab
                    || (key === Qt.Key_Tab && (modifiers & Qt.ShiftModifier))
                    || key === Qt.Key_Left || key === Qt.Key_Up
            return enterNavigation(!backwards)
        }

        let candidate = grid.keyboardIndex
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
        // Emphasis, and nothing else. Hovering used to take the focus as well, so that the
        // next key landed on the hovered tile — which meant that crossing the shelf with the
        // pointer emptied the search field of its focus mid-word. What moves the focus is a
        // click or a key; a pointer passing over something moves only what is highlighted.
        navigationCursor.usePointer(grid, position)
        grid.currentIndex = position
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

    // Declared before the grid because the view reads `header` while it completes, and a
    // Component further down the file is not built yet at that moment.
    Component {
        id: resumeBand

        Item {
            id: above

            // The band, and the room the row stands in below it. Both belong to the header
            // because both sit above the first cover — but only the band travels with it: the
            // row is drawn outside the view and stops at the top, which is what makes one of
            // them leave and the other stay.
            readonly property real bandHeight: band.visible ? band.height : 0
            /// The one stop the band puts in the keyboard chain, named so the view can hand
            /// the focus back to it — see `leaveNavigation`.
            readonly property Item button: band.button

            width: grid.width
            height: bandHeight + (filterPills.visible ? filterPills.height : 0)

            ResumeBand {
                id: band

                width: parent.width
                suppressed: Search.active

            // The step from the band into the grid is made here rather than left to the focus
            // chain. `forceActiveFocus` on a GridView whose `currentIndex` is -1 does not take:
            // the view has no current item to give the focus to, so it stays where it was and the
            // key does nothing at all. It happened to work on a live desktop often enough to look
            // fine, and never once under xvfb, which is what CI runs.
            Keys.onPressed: event => {
                const back = event.key === Qt.Key_Up || event.key === Qt.Key_Backtab
                        || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))
                if (back) {
                    // Upwards from the first thing on the screen is the bar, which is another
                    // screen's business: the boundary signal carries it to the shell.
                    band.focus = false
                    root.navigationCursor.beginKeyboard(false)
                    root.navigationBoundary(grid, false)
                    event.accepted = true
                    return
                }
                if (event.key !== Qt.Key_Down && event.key !== Qt.Key_Right
                        && event.key !== Qt.Key_Tab)
                    return
                root.navigationCursor.beginKeyboard(true)
                // Handed over, not added to. The band is the view's header, so this button is
                // a child of the grid: an item whose descendant already holds the active focus
                // is focused as far as Qt is concerned, and giving the grid the focus takes
                // nothing from the button — the ring would stay lit under a cursor that has
                // moved on, and two things on the screen would claim to be where you are.
                band.focus = false
                if (filterPills.takeFocus(true) || root.enterNavigation(true)) {
                    event.accepted = true
                    return
                }
                root.navigationBoundary(band.button, true)
                event.accepted = true
            }
            }
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
        visible: !Search.active

        header: resumeBand
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

        /// True once the reader has moved the view themselves — a drag, a flick, an arrow.
        /// Not taken from `movementStarted`, which a view emits while it settles itself: that
        /// made it true before the first page had even landed.
        property bool followed: false
        property int held: 0

        // A view whose content only just exceeds its frame settles at the *bottom* of the
        // little range that leaves — 64 pixels, with twelve series — and the band, being the
        // header, is exactly what those 64 pixels cut off. Two things move that room at
        // startup and nothing after: the first page arriving, and the band arriving.
        function stayAtTheBeginning() {
            if (!followed)
                positionViewAtBeginning()
        }

        onDragStarted: followed = true
        onFlickStarted: followed = true
        onCountChanged: {
            const wasEmpty = held === 0
            held = count
            // Also when a filter empties the shelf and fills it again: that is a new list,
            // and a new list is read from its beginning.
            if (wasEmpty && count > 0)
                stayAtTheBeginning()
        }

        // Every one of those moves shows up here — the header growing when the band arrives,
        // a page landing, a filter emptying the list — so this is the one signal to answer.
        // Answered after the last of them and not at each one: called while the view is still
        // rearranging, `positionViewAtBeginning` does nothing at all — the same call a fifth
        // of a second later lands and holds. So the timer waits for the shaking to stop.
        onContentHeightChanged: {
            if (!followed)
                showTheBeginning.restart()
        }

        Timer {
            id: showTheBeginning

            interval: 150
            onTriggered: grid.stayAtTheBeginning()
        }
        currentIndex: -1
        keyNavigationEnabled: true
        keyNavigationWraps: false
        activeFocusOnTab: count > 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        onActiveFocusChanged: {
            // The decision of whether there is anything to enter belongs to the function: a
            // highlight the pointer is painting must not stop the keyboard from taking over.
            if (activeFocus)
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

        delegate: SeriesTile {
            cellWidth: grid.cellWidth
            cellHeight: grid.cellHeight
            coverWidth: grid.coverWidth
            coverHeight: grid.coverHeight
            viewportClearance: root.viewportClearance
            selected: root.navigationCursor.region === grid
                      && root.navigationCursor.index === index
            // Crossing into a tile hands emphasis to the pointer. Motion inside the same
            // tile is deliberately ignored: a stationary hover must not reclaim the cursor
            // just after a keyboard event selected somewhere else.
            onPointerEntered: root.followPointer(index)
            onPointerExited: root.releasePointer(index)
            onOpened: Navigation.open(Navigation.Series, { "series": seriesId })
        }
    }

    // At the top and out of the scroll entirely: "collantes… elles disent ce qu'on regarde",
    // where the band "part au défilement". The band, being the view's header, leaves upward
    // and disappears under this row's bottom edge, which is where the view now begins.
    FilterPills {
        id: filterPills

        z: 2
        width: grid.width
        visible: !Search.active && showing
        // Following the bottom edge of the band, and stopping at the top. That clamp is the
        // whole difference between the two: the band keeps going and leaves, this stays and
        // goes on saying what you are looking at.
        y: grid.headerItem
           ? Math.max(0, grid.originY + grid.headerItem.bandHeight - grid.contentY)
           : 0

        // What lies on either side of the row. It walks its own chips and says when it has
        // been walked past; forward is the covers, backward is the band.
        onWentPast: forward => {
            root.navigationCursor.beginKeyboard(forward)
            if (forward) {
                if (!root.enterNavigation(true))
                    root.navigationBoundary(filterPills, true)
            } else {
                if (!root.focusTheBand())
                    root.navigationBoundary(filterPills, false)
            }
        }
    }

    ScrollVeil {
        objectName: "shelf-scroll-veil-top"
        x: 0
        y: filterPills.visible ? filterPills.y + filterPills.height : 0
        width: root.width
        leading: true
        shown: !Search.active && !grid.atYBeginning
    }

    ScrollVeil {
        objectName: "shelf-scroll-veil-bottom"
        x: 0
        y: root.height - height
        width: root.width
        leading: false
        shown: !Search.active && !grid.atYEnd
    }

    ShelfSkeleton {
        anchors.fill: parent
        // Only when there is nothing to keep. Changing a filter or an order leaves the shelf
        // that is already drawn in place until its replacement lands, so this is the first
        // load and nothing else.
        visible: !Search.active && root.sourceModel.loading && grid.count === 0
        cellWidth: grid.cellWidth
        cellHeight: grid.cellHeight
        coverWidth: grid.coverWidth
        coverHeight: grid.coverHeight
        leftInset: root.viewportClearance
    }

    BusyIndicator {
        objectName: "shelf-next-page"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Widths.shelfMargin
        running: !Search.active && root.sourceModel.loading && grid.count > 0
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
        visible: !Search.active && troubleText.text.length > 0

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

    /// Whether what is on screen is a list of files, which decides what the bar's filter
    /// panel counts. Read by the shell, which owns the bar.
    readonly property bool showingFiles: Search.active && searchWorkspace.showingFiles

    SearchWorkspace {
        id: searchWorkspace

        anchors.fill: parent
        visible: Search.active
        navigationCursor: root.navigationCursor
        onNavigationBoundary: forward => root.navigationBoundary(searchWorkspace, forward)
        onBlankPressed: {
            root.clearNavigation()
            root.pointerNavigationCancelled()
        }
    }
}
