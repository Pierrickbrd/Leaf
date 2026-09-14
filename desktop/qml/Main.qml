// The shell: the paper, the window width shared with every screen, and the Loader that
// navigation will drive. The shelf is its first real destination; later screens replace only
// the Loader's source rather than restructuring the window around themselves.

import QtQuick
import QtQuick.Controls
import Leaf

ApplicationWindow {
    id: window

    readonly property alias navigationCursor: appNavigationCursor

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
        onActivated: Navigation.back()
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
            let origin = window.activeFocusItem
            if (!origin || origin === page || origin === navigationStart) {
                // After a pointer reset every navigation key restarts at position zero;
                // direction matters only once the user is already in the focus chain.
                origin = page
                forward = true
            }
            return window.continueNavigation(origin, forward)
        }

        Keys.onPressed: event => {
            if (!isArrow(event.key) && !isTab(event.key))
                return

            // A composite region owns movement inside itself. BeforeItem is important here:
            // it also records Backtab's direction before Qt performs native focus traversal.
            // A pointer cursor is an anchor even while another item still owns active focus.
            if (appNavigationCursor.region
                    && (appNavigationCursor.mode === appNavigationCursor.pointerMode
                        || window.belongsTo(appNavigationCursor.region,
                                            window.activeFocusItem))) {
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

        Loader {
            id: screen

            anchors.fill: parent
            sourceComponent: shelfScreen
        }
    }

    Component {
        id: shelfScreen

        // This Component deliberately closes over the shell's one cursor and focus root.
        // They are outside its object tree but inside its lexical scope.
        // qmllint disable unqualified
        ShelfView {
            navigationCursor: window.navigationCursor
            onNavigationBoundary: (origin, forward) =>
                                  window.continueNavigation(origin, forward)
            onPointerNavigationCancelled: window.returnFocusToPage()
        }
        // qmllint enable unqualified
    }
}
