// The one bar shared by the application: brand, search, list commands, then settings.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
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

    /// Every value lit, across every axis — not the two the row draws. A reader who narrowed
    /// by genre and closed the panel has to see, on the button, that something is in force.
    readonly property int activeFilters: {
        let total = 0
        for (const axis in Shelf.narrowing)
            total += Shelf.narrowing[axis].length
        return total
    }
    /// Set by the screen below, which is the only thing that knows whether a list of files
    /// is showing. False on the shelf, where there are no files to count.
    property bool filtersCountFiles: false
    readonly property bool searchTakesBar: Widths.band === Widths.Narrow
                                            && searchField.activeFocus

    objectName: "app-bar"
    height: 58
    z: 20

    /// The stops, left to right as they are drawn. A button that is not there is not a stop —
    /// the bar drops its commands when a narrow window gives the search the whole width.
    function stops() {
        const all = []
        for (const one of [searchField, filterButton, sortButton, settingsButton]) {
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
                             ? Search.placeholder : Search.shortPlaceholder
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

            Image {
                id: searchGlyph

                x: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 20
                height: 20
                source: "assets/icons/search.svg"
                sourceSize: Qt.size(20, 20)
                visible: false
            }

            ColorOverlay {
                anchors.fill: searchGlyph
                source: searchGlyph
                color: Theme.inkFaint
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
                Accessible.name: Search.clearLabel
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
                    text: Search.clearLabel
                }
            }
        }

        BarButton {
            id: filterButton

            objectName: "filter-button"
            visible: !bar.searchTakesBar
            source: "assets/icons/tune.svg"
            label: Search.filterLabel
            value: Widths.band !== Widths.Wide && bar.activeFilters > 0
                   ? String(bar.activeFilters) : ""
            held: bar.activeFilters > 0
            toolTipSuppressed: filterPanel.opened
            popup: filterPanel
            onPointerEntered: bar.pointerLeftTheShelf()

            FilterPanel {
                id: filterPanel

                y: filterButton.height + 7
                // Anchored under the button and pulled left, because the button sits in the
                // middle of the bar and a 380 px panel hung from its left edge would run off
                // the right of a narrow window.
                x: -width + filterButton.width
                // The panel counts what the row beneath it counts: above a list of files, it
                // counts files.
                overFiles: bar.filtersCountFiles
            }
        }

        BarButton {
            id: sortButton

            ButtonGroup {
                id: sortCriteria
                exclusive: true
            }

            objectName: "sort-button"
            visible: Widths.band === Widths.Wide && !bar.searchTakesBar
            source: "assets/icons/sort.svg"
            label: Search.sortLabel
            value: Search.sortValue
            toolTipSuppressed: sortMenu.opened
            popup: sortMenu
            onPointerEntered: bar.pointerLeftTheShelf()

            LeafMenu {
                id: sortMenu

                objectName: "sort-menu"
                y: sortButton.height + 7

                Instantiator {
                    model: Search.sortOptions

                    delegate: LeafMenuItem {
                        required property var modelData

                        readonly property bool inForce: Shelf.sort === modelData.value

                        objectName: "sort-option-" + modelData.value
                        // The chosen criterion carries its direction, the others only their
                        // name: without the arrow here nothing says which way the shelf runs,
                        // and the second click that reverses it would have no visible target.
                        text: inForce ? Search.sortValue : modelData.label
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

        Item {
            visible: Widths.band === Widths.Wide && !bar.searchTakesBar
            Layout.fillWidth: true
            Layout.minimumWidth: 8
        }

        BarButton {
            id: settingsButton

            objectName: "settings-button"
            visible: Widths.band === Widths.Wide && !bar.searchTakesBar
            source: "assets/icons/settings.svg"
            label: Search.settingsLabel
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
