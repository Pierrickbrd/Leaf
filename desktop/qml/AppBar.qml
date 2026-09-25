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

    readonly property bool searchTakesBar: Widths.band === Widths.Narrow
                                            && searchField.activeFocus
    /// Everything in this row exists to browse a shelf: a field that searches it, a filter
    /// that narrows it, an order that sorts it. On any other page they act on something
    /// nobody is looking at, so the bar keeps the brand and drops the rest — and the page
    /// carries its own way back, because a control that leaves a page belongs to it.
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
    height: 58
    z: 20

    /// The stops, left to right as they are drawn. A button that is not there is not a stop —
    /// the bar drops its commands when a narrow window gives the search the whole width.
    function stops() {
        const all = []
        for (const one of [searchField, importButton, sortButton, settingsButton]) {
            if (one && one.visible)
                all.push(one)
        }
        return all
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
        spacing: 9

        Item {
            id: brand

            objectName: "leaf-brand"
            visible: !bar.searchTakesBar
            Layout.preferredWidth: mark.width
                                   + (brandWord.visible ? 10 + brandWord.implicitWidth : 0)
            Layout.preferredHeight: 36
            // Thirty-two pixels in all, with the row's own nine: the field is a pill with a
            // focus ring outside its edge, and at the row's spacing alone that ring came to
            // rest against the "f" of Leaf — the name and the field read as one object.
            Layout.rightMargin: 23

            Image {
                id: mark

                objectName: "leaf-mark"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 36
                height: 36
                source: Theme.dark
                        ? "assets/logos/leaf-mark-dark.svg"
                        : "assets/logos/leaf-mark-light.svg"
                sourceSize: Qt.size(36, 36)
            }

            Text {
                id: brandWord

                objectName: "brand-word"
                anchors.left: mark.right
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                visible: Widths.band === Widths.Wide
                text: "Leaf"
                color: Theme.ink
                font.family: Theme.displayFamily
                font.pixelSize: 25
                font.weight: Font.Bold
            }
        }

        TextField {
            id: searchField

            objectName: "search-field"
            visible: bar.browsing
            Layout.fillWidth: true
            Layout.minimumWidth: bar.searchTakesBar ? 120 : 150
            Layout.preferredWidth: 520
            Layout.maximumWidth: bar.searchTakesBar ? 16777215 : 720
            Layout.preferredHeight: 36

            // Out of Qt's traversal, like every other stop of this bar: the order is the
            // page's, and Qt's would consume the key here before the page could say so.
            activeFocusOnTab: false
            text: Shelf.query
            placeholderText: Widths.band === Widths.Wide
                             ? Captions.placeholder : Captions.shortPlaceholder
            color: Theme.ink
            placeholderTextColor: Theme.inkFaint
            selectionColor: Theme.emerald
            selectedTextColor: Theme.onEmerald
            font.family: Theme.textFamily
            font.pixelSize: 15
            leftPadding: 40
            rightPadding: clearSearch.visible ? 42 : 14
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

                x: 12
                anchors.verticalCenter: parent.verticalCenter
                side: 20
                source: "assets/icons/search.svg"
                tint: Theme.inkFaint
            }

            Item {
                id: clearSearch

                objectName: "clear-search"
                anchors.right: parent.right
                anchors.rightMargin: 5
                anchors.verticalCenter: parent.verticalCenter
                width: 28
                height: 28
                visible: searchField.text.length > 0
                activeFocusOnTab: visible

                Accessible.role: Accessible.Button
                Accessible.name: Captions.clearLabel
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
                    onTapped: clearSearch.clear()
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 14
                    height: 1.5
                    radius: 1
                    rotation: 45
                    color: Theme.inkSoft
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 14
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
                    text: Captions.clearLabel
                }
            }
        }

        // The slot the filter left when it went down to the row of pills it fills. An
        // import is a command about the library rather than about what is shown of it,
        // which is why it sits with the order and the settings and not with the search.
        BarButton {
            id: importButton

            objectName: "import-button"
            visible: bar.browsing && !bar.searchTakesBar
            source: "assets/icons/upload.svg"
            label: ImportCaptions.title
            // What it says with the dialog shut: a transfer that finished and a question
            // nobody saw look the same from here, so it can say either.
            value: Imports.deciding > 0 ? ImportCaptions.waitingLabel
                                        : (Imports.inFlight > 0 && Widths.band === Widths.Wide
                                           ? String(Imports.inFlight) : "")
            held: Imports.inFlight > 0
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
            label: Captions.sortLabel
            // The order in force, spelled out, until the window is too narrow to spell it.
            // Only the narrow band drops it: a half-screen window has room for all three
            // commands and their words, and it used to lose two of the commands instead.
            value: Widths.band === Widths.Narrow ? "" : Captions.sortValue
            toolTipSuppressed: sortMenu.opened
            popup: sortMenu
            onPointerEntered: bar.pointerLeftTheShelf()

            LeafMenu {
                id: sortMenu

                objectName: "sort-menu"
                y: sortButton.height + 7

                Instantiator {
                    model: Captions.sortOptions

                    delegate: LeafMenuItem {
                        required property var modelData

                        readonly property bool inForce: Shelf.sort === modelData.value

                        objectName: "sort-option-" + modelData.value
                        // The chosen criterion carries its direction, the others only their
                        // name: without the arrow here nothing says which way the shelf runs,
                        // and the second click that reverses it would have no visible target.
                        text: inForce ? Captions.sortValue : modelData.label
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
            Layout.minimumWidth: 8
        }

        BarButton {
            id: settingsButton

            objectName: "settings-button"
            // Gone from the page it opens: a button that takes you where you already are.
            visible: bar.browsing && !bar.searchTakesBar
            source: "assets/icons/settings.svg"
            label: Captions.settingsLabel
            onTriggered: bar.settingsRequested()
            onPointerEntered: bar.pointerLeftTheShelf()
        }
    }

    Shortcut {
        sequence: "/"
        context: Qt.ApplicationShortcut
        enabled: !searchField.activeFocus
        onActivated: {
            searchField.forceActiveFocus(Qt.ShortcutFocusReason)
            searchField.selectAll()
        }
    }
}
