// Every axis the row cannot draw, under the button that opens them.
//
// The row of pills says what you are looking at; this says what you *could* look at. It
// holds what the row has no width for: a row has four pills' worth and a library has
// twenty-eight authors, so the row keeps the two axes worth a glance and everything else
// lives here, one heading per axis.
//
// **And only what the row cannot draw.** Read status and medium sit permanently as pills an
// inch below the button, so repeating them here offered the same choice twice within one
// glance — see `drawnAlready`. What the row drops for being too long to hold, this picks
// back up, so no axis falls between the two.
//
// Anchored under the button rather than a drawer or a modal, for the same reason the sort
// menu is: a screen with one grammar of opening is a screen a reader learns once. And it
// leaves the shelf visible beside it — you can watch what you are narrowing while you narrow
// it, which is the whole argument for applying a choice as it is made.

import QtQuick
import QtQuick.Controls
import Leaf

Popup {
    id: panel

    /// Replaceable for the same reason `FilterPills` has one: so the QML can be exercised
    /// against stand-ins. The application never assigns either.
    property var source: Filters
    property var shelf: Shelf
    /// Which population the counts describe, the same question the row answers. A panel
    /// opened above a list of files counts files.
    property bool overFiles: false
    /// The axes the row underneath already draws, which this one leaves out. Read status and
    /// medium are permanently on screen as pills, right under the button that opens this, so
    /// offering them here again is the same choice in two places — and the one the hand is
    /// not aiming at is the one it hits by accident. Named rather than hard-coded: the row
    /// drops an axis it cannot hold, and this has to pick it back up when it does.
    property var drawnAlready: []
    /// Short enough to be taken in at a glance, and so unfolded from the start. Its own
    /// number and not `atMostOnScreen`: that one says how tall an axis may get, this one
    /// says how much of the panel may be open at once, and eight axes unfolded is a wall.
    readonly property int shortEnoughToUnfold: 5

    readonly property var axes: {
        const all = overFiles ? source.fileAxes : source.axes
        const kept = []
        for (const one of all) {
            if (drawnAlready.indexOf(one.axis) < 0)
                kept.push(one)
        }
        return kept
    }
    readonly property int litCount: {
        let total = 0
        for (const key in shelf.narrowing)
            total += shelf.narrowing[key].length
        return total
    }

    objectName: "filter-panel"
    width: 380
    // Bounded by the window and never by its own content: a panel that runs off the bottom
    // is a panel whose last axis nobody knows is there.
    height: Math.min(sheet.implicitHeight + 2 * padding,
                     Math.max(240, Overlay.overlay ? Overlay.overlay.height - 96 : 520))
    padding: 8
    margins: 10
    modal: false
    focus: true
    // OutsideParent and not Outside. The panel's parent is the button that opens it, so a
    // press on that button is *inside* the parent and does not close anything — the button's
    // own tap then toggles. With plain `CloseOnPressOutside` the press closed the panel and
    // the release reopened it, so clicking the button while it was open did nothing visible.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    function narrowingWith(axis, value) {
        const narrowing = {}
        for (const key in shelf.narrowing)
            narrowing[key] = shelf.narrowing[key].slice()
        const lit = narrowing[axis] ? narrowing[axis] : []
        const at = lit.indexOf(value)
        if (at >= 0)
            lit.splice(at, 1)
        else
            lit.push(value)
        narrowing[axis] = lit
        return narrowing
    }

    function toggle(axis, value) {
        shelf.filterBy(narrowingWith(axis, value))
    }

    function isLit(axis, value) {
        const lit = shelf.narrowing[axis]
        return lit !== undefined && lit.indexOf(value) >= 0
    }

    function litOn(axis) {
        const lit = shelf.narrowing[axis]
        return lit === undefined ? 0 : lit.length
    }

    background: Item {
        Rectangle {
            x: 2
            y: 6
            width: parent.width
            height: parent.height
            radius: Theme.buttonRadius
            color: "#000000"
            opacity: Theme.dark ? 0.52 : 0.16
        }

        Rectangle {
            x: 1
            y: 2
            width: parent.width
            height: parent.height
            radius: Theme.buttonRadius
            color: "#000000"
            opacity: Theme.dark ? 0.28 : 0.08
        }

        Rectangle {
            objectName: "filter-panel-surface"
            anchors.fill: parent
            radius: Theme.buttonRadius
            color: Theme.surface
            border.color: Theme.rule
            border.width: 1
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 110 }
    }

    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80 }
    }

    contentItem: Item {
        id: sheet

        implicitHeight: heading.height + axesColumn.height

        Item {
            id: heading

            objectName: "filter-panel-heading"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 32

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: Captions.filterLabel
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: 12
                font.weight: Font.Medium
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.8
            }

            // Shown only when there is something to clear: a command that can do nothing is
            // a command a reader has to read before learning it does nothing.
            LeafTextAction {
                objectName: "clear-every-filter"
                anchors.right: parent.right
                anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                visible: panel.litCount > 0
                label: Captions.clearFiltersLabel
                onTriggered: panel.shelf.filterBy({})
            }
        }

        Flickable {
            id: list

            objectName: "filter-panel-axes"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: heading.bottom
            anchors.bottom: parent.bottom
            contentWidth: width
            contentHeight: axesColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: axesColumn

                width: list.width
                spacing: 2

                Repeater {
                    model: panel.axes

                    delegate: FilterAxis {
                        id: one

                        required property var modelData

                        width: axesColumn.width
                        axis: modelData.axis
                        title: modelData.title
                        values: modelData.values
                        litHere: panel.litOn(modelData.axis)
                        // Short axes open because a reader takes them in at a glance; long
                        // ones folded, because eight axes unfolded is a wall and a wall is
                        // read by nobody. An axis holding a lit value opens whatever its
                        // length: a filter in force must never be out of sight.
                        startsOpen: modelData.values.length <= panel.shortEnoughToUnfold
                                    || litHere > 0
                        lit: (value) => panel.isLit(modelData.axis, value)
                        onPicked: value => panel.toggle(modelData.axis, value)
                    }
                }

                Text {
                    objectName: "filter-panel-nothing"
                    x: 6
                    width: axesColumn.width - 12
                    visible: panel.axes.length === 0
                    text: Captions.nothingToFilterLabel
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    topPadding: 6
                    bottomPadding: 10
                }
            }
        }
    }
}
