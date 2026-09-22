// The shell: the paper, the window width shared with every screen, and the Loader that
// navigation will drive. The shelf is its first real destination; later screens replace only
// the Loader's source rather than restructuring the window around themselves.

import QtQuick
import QtQuick.Controls
import Leaf

ApplicationWindow {
    id: window

    readonly property alias navigationCursor: appNavigationCursor
    /// Whatever the page is showing: the Loader's screen when there is one, the shelf
    /// underneath it otherwise. The shelf is no longer the Loader's business, so nothing
    /// may ask the Loader what is on screen.
    readonly property var showing: screen.item ? screen.item : shelf

    function belongsTo(owner, item) {
        let candidate = item
        while (candidate) {
            if (candidate === owner)
                return true
            candidate = candidate.parent
        }
        return false
    }

    function clearNavigation() {
        appNavigationCursor.reset()
    }

    /// Hands the focus from the bar to whatever the Loader is showing, by its first stop
    /// going forward and its last coming back. A screen that cannot take it — an empty shelf
    /// has nothing to focus — sends it straight round to the other end of the bar.
    function moveIntoScreen(forward) {
        appNavigationCursor.beginKeyboard(forward)
        const view = window.showing
        if (view && view.takeFocus && view.takeFocus(forward))
            return
        appBar.takeFocus(!forward)
    }

    /// And back out of it, wrapping: past the last cover is the bar's first stop, before the
    /// band is the bar's last.
    function leaveScreen(forward) {
        appNavigationCursor.beginKeyboard(forward)
        appBar.takeFocus(forward)
    }

    function returnFocusToPage() {
        clearNavigation()
        navigationStart.forceActiveFocus(Qt.MouseFocusReason)
    }

    function continueNavigation(origin, forward) {
        appNavigationCursor.beginKeyboard(forward)
        const next = origin.nextItemInFocusChain(forward)
        if (next && next !== origin && next !== page) {
            next.forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
            return true
        }

        appNavigationCursor.clear()
        return false
    }

    width: 1100
    height: 760
    visible: true
    title: qsTr("Leaf")

    color: Theme.paper

    onActiveChanged: {
        if (!active)
            clearNavigation()
    }
    onActiveFocusItemChanged: {
        appNavigationCursor.noteFocus(activeFocusItem)
        const owner = appNavigationCursor.region
        if (owner && activeFocusItem && !belongsTo(owner, activeFocusItem))
            clearNavigation()
    }

    // The three widths are declared once, in Widths, and fed from here — the one place that
    // knows how wide the window is.
    onWidthChanged: Widths.window = window.width
    Component.onCompleted: Widths.window = window.width

    // Escape comes back. It is here rather than on each screen so that a screen written later
    // cannot forget it.
    Shortcut {
        // StandardKey is a global QML enumeration: there is deliberately no object to qualify.
        // qmllint disable unqualified
        sequence: StandardKey.Cancel
        // qmllint enable unqualified
        onActivated: {
            // On the shelf Escape first clears the transient question. Only an already-empty
            // field means "leave this destination".
            if (Shelf.query.length > 0)
                Shelf.searchFor("")
            else
                Navigation.back()
        }
    }

    // Focus is a page-wide concern. The cursor deliberately lives beside the Loader rather
    // than inside ShelfView: the toolbar, filters and later screens will join this same chain
    // without each inventing a private notion of keyboard versus pointer emphasis.
    NavigationCursor {
        id: appNavigationCursor

        objectName: "navigation-cursor"
    }

    FocusScope {
        id: page

        objectName: "page-navigation-root"
        anchors.fill: parent
        focus: true
        Keys.priority: Keys.BeforeItem

        function isArrow(key) {
            return key === Qt.Key_Left || key === Qt.Key_Right
                    || key === Qt.Key_Up || key === Qt.Key_Down
        }

        function isTab(key) {
            return key === Qt.Key_Tab || key === Qt.Key_Backtab
        }

        function goesForward(key, modifiers) {
            if (key === Qt.Key_Left || key === Qt.Key_Up || key === Qt.Key_Backtab)
                return false
            if (key === Qt.Key_Tab)
                return !(modifiers & Qt.ShiftModifier)
            return true
        }

        function beginNavigation(forward) {
            const origin = window.activeFocusItem
            if (!origin || origin === page || origin === navigationStart) {
                // From nothing, the page starts where the eye starts: the bar. Direction does
                // not matter here — after a click on bare paper every key restarts there, and
                // the order runs bar, band, row, covers, and round again from either end.
                appNavigationCursor.beginKeyboard(true)
                return appBar.takeFocus(true)
            }
            return window.continueNavigation(origin, forward)
        }

        Keys.onPressed: event => {
            if (!isArrow(event.key) && !isTab(event.key))
                return

            // Left and Right edit the field. They join global navigation everywhere else,
            // including Up and Down from that same field.
            if (window.activeFocusItem
                    && window.activeFocusItem.objectName === "search-field"
                    && (event.key === Qt.Key_Left || event.key === Qt.Key_Right))
                return

            // A composite region owns movement inside itself. BeforeItem is important here:
            // it also records Backtab's direction before Qt performs native focus traversal.
            // A pointer cursor is an anchor even while another item still owns active focus.
            // Keys follow the focus, and only the focus. A pointer resting somewhere used to
            // claim them too, which is how an arrow pressed while typing went to whatever the
            // mouse happened to be over instead of to the field it was typed in.
            if (appNavigationCursor.region
                    && window.belongsTo(appNavigationCursor.region,
                                        window.activeFocusItem)) {
                appNavigationCursor.requestNavigationKey(event.key, event.modifiers)
                event.accepted = true
                return
            }
            event.accepted = beginNavigation(goesForward(event.key, event.modifiers))
        }

        // A FocusScope remembers its last focused child. This neutral child prevents a click
        // on bare paper from silently restoring the grid that was meant to be forgotten.
        Item {
            id: navigationStart

            objectName: "navigation-start"
            width: 0
            height: 0
        }

        AppBar {
            id: appBar

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            // The highlight goes, the focus stays. Returning the focus to the page here is
            // what emptied the field of its focus when a pointer merely crossed a button.
            onPointerLeftTheShelf: window.clearNavigation()
            // Out of the bar and into the screen below — or round the other way, the screen
            // being the only other thing on the page. This is the whole of the order the eye
            // reads: bar, band, chips, covers, and back to the bar.
            onWentPast: forward => window.moveIntoScreen(forward)
            onSettingsRequested: Navigation.open(Navigation.Settings)
            onImportRequested: importDialog.show()
        }

        // Built once and hidden, never destroyed. Inside the Loader it was torn down on the
        // way to the settings and rebuilt on the way back: the scroll went to the top, every
        // cover was decoded again and the skeleton flashed over a page the reader had already
        // read. The shelf is the one screen you always come back to, so it is the one screen
        // that stays.
        ShelfView {
            id: shelf

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: appBar.bottom
            anchors.bottom: parent.bottom
            visible: Navigation.destination === Navigation.Shelf
            navigationCursor: window.navigationCursor
            onNavigationBoundary: (origin, forward) => window.leaveScreen(forward)
            onPointerNavigationCancelled: window.returnFocusToPage()
        }

        Loader {
            id: screen

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: appBar.bottom
            anchors.bottom: parent.bottom
            // Every destination that is not the shelf, drawn over it. It was pinned to the
            // shelf, which is why the settings button had to be left unwired: opening a
            // destination nothing draws changed the stack, showed the same page, and spent
            // the next Escape popping something nobody had seen.
            sourceComponent: {
                switch (Navigation.destination) {
                case Navigation.Settings:
                    return settingsScreen
                default:
                    return null
                }
            }
        }
    }

    // Dropped anywhere in the window, not only on the dialog. Letting a volume go over the
    // shelf and having nothing happen is the kind of silence a reader reads as "it does not
    // work here", and the answer is one line: open the dialog with it inside.
    DropArea {
        objectName: "window-drop"
        anchors.fill: parent
        onDropped: drop => {
            if (Imports.offerUrls(drop.urls) > 0) {
                importDialog.phase = importDialog.proposing
                importDialog.open()
            }
        }
    }

    ImportDialog {
        id: importDialog
    }

    Component {
        id: settingsScreen

        SettingsView { }
    }
}
