// The one bar shared by the application: brand, search, list commands, then settings.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Leaf

Item {
    id: bar

    signal settingsRequested()
    /// The pointer has entered the bar, so whatever the shelf was highlighting is no
    /// longer where the reader is looking. It clears that highlight and nothing else: a
    /// pointer crossing a button must not take the focus out of the field being typed in.
    signal pointerLeftTheShelf()

    /// The bar was walked past, in that direction: forward is into the screen below, backward
    /// is out of the bar the other way. The bar walks its own stops and says when it has been
    /// walked past; where the page goes next is the page's business, not the bar's.
    signal wentPast(bool forward)

    /// The page opens the dialog, not the bar: the queue outlives this row, and a button
    /// that owned the window onto it would take it down when the bar stopped browsing.
    signal importRequested()
    /// The square was pressed: the page opens « Qui lit ? », over the whole window.
    signal whoReadsRequested()

    readonly property bool searchTakesBar: Widths.band === Widths.Narrow
                                            && searchField.activeFocus
    /// Everything in this row exists to browse a shelf: a field that searches it, a filter
    /// that narrows it, an order that sorts it. On any other page they act on something
    /// nobody is looking at, so the bar keeps the brand and drops the rest.
    readonly property bool browsing: Navigation.destination === Navigation.Shelf

    // A popup outlives the button that opened it: hiding the button hides nothing that is
    // already on screen. Leaving the shelf with the order menu down left it hanging over
    // the settings, with no button left to shut it. The filter panel went down to the row
    // of pills and is closed by the same rule there, the row going with the shelf.
    onBrowsingChanged: {
        if (!browsing)
            sortMenu.close()
    }

    objectName: "app-bar"
    height: Widths.scale.size(58)
    z: 20

    /// The stops, left to right as they are drawn. A button that is not there is not a stop —
    /// the bar drops its commands when a narrow window gives the search the whole width.
    function stops() {
        const all = []
        for (const one of [searchField, importButton, sortButton, settingsButton, profileButton]) {
            if (one && one.visible)
                all.push(one)
        }
        return all
    }

    function takeFocusOnSquare() {
        if (profileButton.visible)
            profileButton.forceActiveFocus(Qt.OtherFocusReason)
    }

    function at(all) {
        for (let i = 0; i < all.length; ++i) {
            if (all[i].activeFocus)
                return i
        }
        return -1
    }

    /// Takes the focus by its first stop going forward, its last coming back.
    function takeFocus(forward) {
        const all = stops()
        if (all.length === 0)
            return false
        all[forward ? 0 : all.length - 1].forceActiveFocus(
            forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
        return true
    }

    // Tab and the arrows walk the bar; past either end the page takes over. Left and Right
    // are left to the field, which is a text field and needs them to move a caret.
    Keys.onPressed: event => {
        const all = stops()
        const here = at(all)
        if (here < 0)
            return

        const typing = all[here] === searchField
        const back = event.key === Qt.Key_Backtab || event.key === Qt.Key_Up
                || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))
                || (!typing && event.key === Qt.Key_Left)
        const on = event.key === Qt.Key_Tab || event.key === Qt.Key_Down
                || (!typing && event.key === Qt.Key_Right)
        if (!back && !on)
            return

        event.accepted = true
        const leaves = event.key === Qt.Key_Down || event.key === Qt.Key_Up
        const to = here + (on ? 1 : -1)
        if (leaves || to < 0 || to >= all.length) {
            bar.wentPast(on)
            return
        }
        all[to].forceActiveFocus(on ? Qt.TabFocusReason : Qt.BacktabFocusReason)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.surface
    }

    // The artifact's small bar elevation: a single restrained line, not a card shadow.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.rule
        opacity: Theme.dark ? 0.7 : 0.45
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Widths.shelfMargin
        anchors.rightMargin: Widths.shelfMargin
        spacing: Widths.space.label

        BarBrand {
            id: brand

            objectName: "leaf-brand"
            visible: !bar.searchTakesBar
            worded: Widths.band === Widths.Wide
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            // A `section` from the name to whatever follows it — the field on the shelf —
            // the row's own `label` included. At the row's spacing alone the field's focus
            // ring, drawn outside its edge, came to rest against the "f" of Leaf, and the
            // name and the field read as one object.
            Layout.rightMargin: Widths.space.section - Widths.space.label
        }

        TextField {
            id: searchField

            objectName: "search-field"
            visible: bar.browsing
            Layout.fillWidth: true
            Layout.minimumWidth: Widths.scale.size(bar.searchTakesBar ? 120 : 150)
            Layout.preferredWidth: Widths.scale.column(520)
            // A field of words, so it grows as a column: grown as a length it would reach 900
            // pixels at 1920, with the query at one end of it.
            Layout.maximumWidth: bar.searchTakesBar ? 16777215 : Widths.scale.column(720)
            Layout.preferredHeight: Widths.scale.size(36)

            // Out of Qt's traversal, like every other stop of this bar: the order is the
            // page's, and Qt's would consume the key here before the page could say so.
            activeFocusOnTab: false
            text: Shelf.query
            placeholderText: Widths.band === Widths.Wide
                             ? BarCaptions.placeholder : BarCaptions.shortPlaceholder
            color: Theme.ink
            placeholderTextColor: Theme.inkFaint
            selectionColor: Theme.emerald
            selectedTextColor: Theme.onEmerald
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(15)
            // The glyph a `card` in from the pill's end, its words a `label` after it; the
            // cross mirrors the glyph at the other end, and the words stop as far from it.
            leftPadding: Widths.space.card + searchGlyph.width + Widths.space.label
            rightPadding: clearSearch.visible ? leftPadding : Widths.space.card
            verticalAlignment: TextInput.AlignVCenter

            onTextEdited: Shelf.searchFor(text)

            background: Rectangle {
                color: Theme.onBar
                radius: searchField.height / 2
                border.color: searchField.activeFocus ? Theme.emerald : "transparent"
                border.width: 1
            }

            FocusRing {
                objectName: "search-field-focus"
                cornerRadius: searchField.height / 2
            }

            Glyph {
                id: searchGlyph

                x: Widths.space.card
                anchors.verticalCenter: parent.verticalCenter
                side: Widths.scale.icon(20)
                source: "assets/icons/search.svg"
                tint: Theme.inkFaint
            }

            Item {
                id: clearSearch

                objectName: "clear-search"
                anchors.right: parent.right
                anchors.rightMargin: Widths.space.card - (width - searchGlyph.width) / 2
                anchors.verticalCenter: parent.verticalCenter
                width: Widths.scale.size(28)
                height: width
                visible: searchField.text.length > 0
                activeFocusOnTab: visible

                Accessible.role: Accessible.Button
                Accessible.name: BarCaptions.clearLabel
                Accessible.focusable: true
                Accessible.focused: activeFocus
                Accessible.onPressAction: clearSearch.clear()

                function clear() {
                    Shelf.searchFor("")
                    searchField.forceActiveFocus(Qt.ShortcutFocusReason)
                }

                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                            || event.key === Qt.Key_Enter) {
                        clear()
                        event.accepted = true
                    }
                }

                HoverHandler {
                    id: clearPointer

                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: {
                        if (hovered)
                            bar.pointerLeftTheShelf()
                    }
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: clearSearch.clear()
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: Widths.scale.icon(14)
                    height: 1.5
                    radius: 1
                    rotation: 45
                    color: Theme.inkSoft
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: Widths.scale.icon(14)
                    height: 1.5
                    radius: 1
                    rotation: -45
                    color: Theme.inkSoft
                }

                FocusRing {
                    objectName: "clear-search-focus"
                    cornerRadius: clearSearch.height / 2
                }

                LeafToolTip {
                    objectName: "clear-search-tooltip"
                    visible: clearPointer.hovered
                    text: BarCaptions.clearLabel
                }
            }
        }

        // The slot the filter left when it went down to the row of pills it fills. An
        // import is a command about the library rather than about what is shown of it,
        // which is why it sits with the order and the settings and not with the search.
        BarButton {
            id: importButton

            objectName: "import-button"
            // Not offered to a profile without the right: the server would refuse what it sent.
            visible: bar.browsing && !bar.searchTakesBar && Profiles.canImport
            source: "assets/icons/upload.svg"
            label: ImportCaptions.title
            // What it says with the dialog shut: a transfer that finished and a question
            // nobody saw look the same from here, so it can say either. A card stopped to be
            // checked is such a question too.
            value: Imports.deciding + Imports.reviewing > 0 ? ImportCaptions.waitingLabel
                                        : (ImportTally.inFlight > 0 && Widths.band === Widths.Wide
                                           ? String(ImportTally.inFlight) : "")
            held: ImportTally.inFlight > 0
            onTriggered: bar.importRequested()
            onPointerEntered: bar.pointerLeftTheShelf()
        }

        BarButton {
            id: sortButton

            ButtonGroup {
                id: sortCriteria
                exclusive: true
            }

            objectName: "sort-button"
            visible: bar.browsing && !bar.searchTakesBar
            source: "assets/icons/sort.svg"
            label: BarCaptions.sortLabel
            // The order in force, spelled out, until the window is too narrow to spell it.
            // Only the narrow band drops it: a half-screen window has room for all three
            // commands and their words, and it used to lose two of the commands instead.
            value: Widths.band === Widths.Narrow ? "" : BarCaptions.sortValue
            toolTipSuppressed: sortMenu.opened
            popup: sortMenu
            onPointerEntered: bar.pointerLeftTheShelf()

            LeafMenu {
                id: sortMenu

                objectName: "sort-menu"
                y: sortButton.height + Widths.space.compact

                Instantiator {
                    model: BarCaptions.sortOptions

                    delegate: LeafMenuItem {
                        required property var modelData

                        readonly property bool inForce: Shelf.sort === modelData.value

                        objectName: "sort-option-" + modelData.value
                        // The chosen criterion carries its direction, the others only their
                        // name: without the arrow here nothing says which way the shelf runs,
                        // and the second click that reverses it would have no visible target.
                        text: inForce ? BarCaptions.sortValue : modelData.label
                        checkable: true
                        checked: inForce
                        ButtonGroup.group: sortCriteria
                        onTriggered: Shelf.sortBy(modelData.value)
                    }

                    onObjectAdded: (index, object) => sortMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => sortMenu.removeItem(object)
                }

            }
        }

        // What holds the settings against the right edge, away from the two commands that
        // act on the shelf. It goes only when the field takes the whole bar.
        Item {
            visible: !bar.searchTakesBar
            Layout.fillWidth: true
            Layout.minimumWidth: Widths.space.label
        }

        BarButton {
            id: settingsButton

            objectName: "settings-button"
            // Not one of the shelf's commands, so not gone with them: the settings are the
            // application's, and a series page that dropped the gear left a reader there with
            // no way to them but back to the shelf first. Gone only from the page it opens —
            // a button that takes you where you already are.
            visible: Navigation.destination !== Navigation.Settings && !bar.searchTakesBar
            source: "assets/icons/settings.svg"
            label: BarCaptions.settingsLabel
            onTriggered: bar.settingsRequested()
            onPointerEntered: bar.pointerLeftTheShelf()
        }

        // Who reads, at the far right, after the gear: the way to « Qui lit ? », there even
        // with a single profile, because that screen is also the way to « Ajouter ». No name
        // beside it — the colour and the initial are the profile's whole signature. Absent
        // until the server has said who is seated.
        Item {
            id: profileButton

            objectName: "profile-square"
            visible: !bar.searchTakesBar && Profiles.active.id !== undefined
            // As tall as the bar's buttons beside it: it was a literal 34 while they grew with
            // the window, and at 1920 stood 8 pixels shorter than the gear.
            Layout.preferredWidth: Widths.scale.size(34)
            Layout.preferredHeight: Widths.scale.size(34)
            Layout.leftMargin: Widths.scale.size(4)
            activeFocusOnTab: false

            Accessible.role: Accessible.Button
            Accessible.name: Profiles.active.name ?? ""
            Accessible.focusable: true
            Accessible.focused: activeFocus
            Accessible.onPressAction: bar.whoReadsRequested()

            Keys.onPressed: event => {
                if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                        || event.key === Qt.Key_Enter) {
                    bar.whoReadsRequested()
                    event.accepted = true
                }
            }

            // The ring « Qui lit ? » draws round the profile reading now: the profile's colour,
            // close round the square, and behind it, so the crown or the lock stands over it
            // and hides the stretch it would cross. Lit by the keys alone, as every ring.
            Rectangle {
                objectName: "profile-square-focus"
                readonly property var cursor: Window.window ? Window.window.navigationCursor
                                                           : null

                visible: profileButton.activeFocus && !(cursor && cursor.pointerTookOver)
                anchors.centerIn: face
                width: face.side + 2 * Theme.focusGap
                height: width
                radius: face.radius + Theme.focusGap
                color: "transparent"
                border.color: face.color
                border.width: 2
                antialiasing: true
            }

            ProfileSquare {
                id: face

                objectName: "profile-square-face"
                side: profileButton.width
                profile: Profiles.active
            }

            HoverHandler {
                id: squarePointer

                cursorShape: Qt.PointingHandCursor
                onHoveredChanged: {
                    if (hovered)
                        bar.pointerLeftTheShelf()
                }
            }

            TapHandler {
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: bar.whoReadsRequested()
            }

            LeafToolTip {
                visible: squarePointer.hovered
                text: Profiles.active.name ?? ""
                // A name typed on another device — « <b>x</b> » — is shown as typed.
                textFormat: Text.PlainText
            }
        }
    }

    Shortcut {
        sequence: "/"
        // The window's, which a modal stops: as the application's it fired under a
        // confirmation and took the focus out of it, for a field behind its veil.
        context: Qt.WindowShortcut
        // A shortcut ignores that the bar is disabled under « Qui lit ? ». Not in the reader
        // either, which has keys of its own and no bar on screen: the focus went to a field
        // nobody could see, and the reader's keys with it.
        enabled: !searchField.activeFocus && bar.enabled
                 && Navigation.destination !== Navigation.Reader
        onActivated: {
            // The search is the shelf's. Pressed on another page, it focused the field that
            // page hides, and typing then went nowhere; it brings the shelf back instead —
            // pushed, so that Escape returns to the page it was pressed on.
            if (!bar.browsing)
                Navigation.open(Navigation.Shelf, {})
            searchField.forceActiveFocus(Qt.ShortcutFocusReason)
            searchField.selectAll()
        }
    }
}
