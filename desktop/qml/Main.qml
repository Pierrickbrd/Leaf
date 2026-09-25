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
    /// Set when something changed a tile of the wall while the wall was not on screen, and
    /// answered on the way back. Held here rather than on `Shelf`, because it is not a fact
    /// about the library: it is a fact about what is drawn over it.
    property bool shelfIsStale: false

    /// The wall, brought up to date where somebody is looking at it and marked out of date
    /// where nobody is. Both halves matter: asked for at every mark it would be five
    /// libraries nobody looked at, and never asked for it would go on saying « 4 lus » under
    /// a tile whose fifth was just marked from that tile's own menu.
    function refreshTheShelf() {
        if (Navigation.destination === Navigation.Shelf)
            Shelf.reload()
        else
            window.shelfIsStale = true
    }

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
                case Navigation.Series:
                    return seriesScreen
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

    // Over everything and taking no room from it: a bubble is a layer, and the screen under
    // it must not move when one appears.
    ToastStack { }

    // What warns, connected to what happens. Every sentence is written in C++ — a `.qml` that
    // composed one would be a `.qml` writing French, and the next one would write it
    // differently.
    Connections {
        target: Imports

        function onSettled(went, subject, said) {
            Toasts.importSettled(went, subject, said)
        }
    }

    Connections {
        target: Scan

        function onFinished() {
            if (Scan.trouble.length > 0)
                Toasts.scanFailed(Scan.trouble)
            else
                Toasts.scanFinished(Scan.counts)
        }
    }

    Connections {
        target: Commands

        function onSaved(where) {
            Toasts.copySaved(where)
        }

        function onChanged() {
            if (Commands.trouble.length > 0)
                Toasts.commandRefused(Commands.trouble)
        }
    }

    // What a bubble offered, done. The bubble knows what was said and not what to do about
    // it, which is why it asks here.
    Connections {
        target: Toasts

        function onActed(what, subject) {
            // The shelf, refreshed: what just landed is on it, and there is no series
            // identifier to open — an import knows the folder it was given and not what the
            // scan made of it.
            if (what === Toasts.See) {
                Navigation.open(Navigation.Shelf, {})
                window.refreshTheShelf()
            }
            else if (what === Toasts.Retry)
                importDialog.show()
            else if (what === Toasts.OpenFolder)
                Commands.showTheFolder(subject)
        }
    }

    // The one confirmation that stands in front of something a scan will not undo. Here
    // rather than on the page: the same modal opens from a shelf tile, and the shelf is not
    // the page.
    EraseDialog { }

    ImportDialog {
        id: importDialog
    }

    Component {
        id: settingsScreen

        SettingsView { }
    }

    Component {
        id: seriesScreen

        SeriesView {
            // The import belongs to the window, not to a row: a dialog opened from inside a
            // list would go with the list the moment its model answers again.
            onReimportAsked: importDialog.show()
        }
    }

    // The page is pointed at what navigation asked for, and the list follows once the page
    // knows its gaps: a hole belongs to the series and not to its files, so it travels from
    // one model to the other rather than being asked for twice.
    //
    // Here rather than inside `SeriesView`, because a screen that fetches on creation would
    // fetch again every time the Loader rebuilt it — and the whole arrangement of this page
    // is that nothing is thrown away to be asked for a second time.
    Connections {
        target: Navigation

        function onChanged() {
            // Back on the wall, once, with whatever changed under it while it was hidden.
            if (Navigation.destination === Navigation.Shelf && window.shelfIsStale) {
                Shelf.reload()
                window.shelfIsStale = false
            }
            // Off the page: what it was holding is let go. Kept, the next series opened on
            // the last one's cover and title for as long as its answer took — and a shelf
            // seen in between did not make that any less wrong.
            if (Navigation.destination !== Navigation.Series) {
                Series.forget()
                Entries.forget()
                Elsewhere.forget()
                return
            }
            const asked = Navigation.parameters.series ?? ""
            if (asked.length > 0 && asked !== Series.identifier)
                Series.point(asked)
        }
    }

    Connections {
        target: Series

        function onChanged() {
            if (!Series.available || Series.identifier === Entries.pointedAt)
                return
            Entries.point(Series.identifier, Series.missingVolumes, Series.arcCount)
            // The universe block follows the same page. Pointed even at a series that belongs
            // to no universe, because that is how it learns to draw nothing: a block still
            // holding the last universe would offer the wrong places to go.
            Elsewhere.point(Series.universeId, Series.universe, Series.identifier)
        }
    }

    // What a command changed, brought back to what is showing it. The page does not guess —
    // a mark set from a menu is a fact on the server, and the screen asks again rather than
    // moving its own rows to match what it just sent — but it asks for what moved, and only
    // where somebody is looking. Asking for all of it turned one word into four answers and
    // a grid rebuilt from nothing, which is what a reader saw as the page reloading.
    Connections {
        target: Commands

        function onMarked(seriesId, entryId) {
            if (seriesId.length > 0 && seriesId === Series.identifier) {
                // The counts in the header moved — « 4 lus · le 5ᵉ en cours ». One answer,
                // and the page keeps what it shows until it lands.
                Series.reload()
                // Not `reload`: the list did not change. The same files, in the same order,
                // with one state different — so only the states are asked for again.
                Entries.refreshProgress()
            }
            // The band is on this page as much as on the shelf, and marking a volume moves
            // where one resumes.
            Resume.reload()
            window.refreshTheShelf()
        }
    }

    // Gone from the disk. A page showing an edition that no longer exists goes back to the
    // shelf rather than staying on a header nothing can answer for.
    Connections {
        target: Erasure

        function onErased(seriesId, entryId) {
            window.refreshTheShelf()
            Resume.reload()
            if (entryId.length > 0) {
                if (seriesId === Series.identifier) {
                    Series.reload()
                    Entries.reload()
                }
                return
            }
            if (Navigation.destination === Navigation.Series
                    && seriesId === Series.identifier)
                Navigation.back()
        }
    }

    // Where the reader stands, handed to the block that writes « ici »: a reading order may
    // send the same work round twice, and only the list of volumes knows which stretch is the
    // one being read. Bound here rather than read by the block, which would then have to know
    // about a model it has nothing else to do with.
    Connections {
        target: Entries

        function onChanged() {
            if (Series.identifier === Elsewhere.pointedAt)
                Elsewhere.readAt(Entries.reading)
        }
    }
}
