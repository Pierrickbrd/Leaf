// The row that says what you are looking at.
//
// Two axes, separated by a rule: what you have read on the left, what kind of book on the
// right. Which values are worth offering is decided in `Filters` and never here — an axis that
// does not cut the library in two never reaches this file, so a row that draws nothing is a
// library with nothing to filter by, not a bug.
//
// **Sticky, where the band is not.** The artifact separates them in one line each: the band
// "part au défilement", the pills are "collantes sous la barre — elles disent ce qu'on
// regarde". A statement about what you are seeing has to stay visible while you scroll
// through what it selected. So the band slides *under* this row and out, and this row climbs
// until it reaches the top and stops — which is why it carries an opaque background rather
// than letting the paper show through.
//
// Lighting a pill is an "or" inside its axis and an "and" across them, because that is what
// the contract does with a repeated parameter and what anyone expects a row of chips to do.
// The lists go to `Shelf` in the contract's own spelling; nothing here interprets them.

import QtQuick
import Leaf

Item {
    id: row

    // Replaceable for the same reason ShelfView's `sourceModel` is: so the QML can be
    // exercised with stand-ins. The application never assigns either.
    property var source: Filters
    property var shelf: Shelf
    property bool reloadOnCompleted: true
    /// Which population the counts describe. A row drawn above a list of files has to count
    /// files: « Non lues 5 » over sixty file rows counts five *series*, which is neither what
    /// the reader is looking at nor what the pill under their finger would leave standing.
    property bool overFiles: false

    readonly property var readStatusValues: overFiles ? source.fileReadStatuses
                                                      : source.readStatuses
    readonly property var mediumValues: overFiles ? source.fileMedia : source.media

    /// Everything lit on an axis this row does not draw — a genre, an author, a publisher.
    /// Narrowing by « Horreur » and closing the panel would otherwise leave a shelf of three
    /// series with nothing on screen saying why. A lit value is not an offer, it is a
    /// statement about what you are looking at, which is the whole job of this row: it is
    /// shown whatever the axis, and clicking it puts it out.
    readonly property var elsewhereValues: {
        const shown = []
        const offered = overFiles ? source.fileAxes : source.axes
        for (const axis in shelf.narrowing) {
            if (axis === "read" || axis === "medium")
                continue
            for (const value of shelf.narrowing[axis]) {
                let label = value
                for (const one of offered) {
                    if (one.axis !== axis)
                        continue
                    for (const known of one.values) {
                        if (known.value === value)
                            label = known.label
                    }
                }
                shown.push({ "axis": axis, "value": value, "label": label })
            }
        }
        return shown
    }

    readonly property bool showing: readStatusValues.length > 0 || mediumValues.length > 0
                                    || elsewhereValues.length > 0

    objectName: "filter-pills"
    visible: showing
    height: showing ? chips.height + 2 * Widths.shelfGap : 0

    /// The row was passed through, in that direction: forward is towards the covers, backward
    /// towards the band. The row knows how to walk its own chips and nothing else — where the
    /// screen goes next is the screen's business.
    signal wentPast(bool forward)

    Component.onCompleted: {
        if (reloadOnCompleted)
            source.reload()
    }

    /// The chips, in the order they are drawn. Taken from the Row rather than from the two
    /// Repeaters, because that is the order a reader sees — and the rule between them is not
    /// a stop, having no `activeFocusOnTab` of its own.
    function stops() {
        const all = []
        for (let i = 0; i < chips.children.length; ++i) {
            const one = chips.children[i]
            if (one && one.visible && one.filterChip !== undefined)
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

    /// Takes the focus by its first chip going forward, its last coming back. False when there
    /// is no chip to take it — an axis that cuts nothing is not drawn, so this row is often
    /// not there at all, and the band then hands straight over to the covers.
    function takeFocus(forward) {
        const all = stops()
        if (all.length === 0)
            return false
        all[forward ? 0 : all.length - 1].forceActiveFocus(
            forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
        return true
    }

    /// The whole selection with one value turned on or off, handed back as one map. The
    /// shelf holds every axis together because it asks for them together; two lists could
    /// disagree about what is being narrowed.
    function toggle(axis, value) {
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
        shelf.filterBy(narrowing)
    }

    function isLit(axis, value) {
        const lit = shelf.narrowing[axis]
        return lit !== undefined && lit.indexOf(value) >= 0
    }

    // Left and right walk the row; past either end, the screen takes over. Down and Up leave
    // it outright — the row is one line, so there is nowhere else for them to go.
    Keys.onPressed: event => {
        const all = stops()
        const here = at(all)
        if (here < 0)
            return

        const back = event.key === Qt.Key_Left || event.key === Qt.Key_Up
                || event.key === Qt.Key_Backtab
                || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))
        const on = event.key === Qt.Key_Right || event.key === Qt.Key_Down
                || event.key === Qt.Key_Tab
        if (!back && !on)
            return

        event.accepted = true
        const leaves = event.key === Qt.Key_Down || event.key === Qt.Key_Up
        const to = here + (on ? 1 : -1)
        if (leaves || to < 0 || to >= all.length) {
            row.wentPast(on)
            return
        }
        all[to].forceActiveFocus(on ? Qt.TabFocusReason : Qt.BacktabFocusReason)
    }

    // The band passes behind this, so the paper cannot show through it.
    Rectangle {
        anchors.fill: parent
        color: Theme.paper
    }

    Row {
        id: chips

        objectName: "filter-chips"
        x: Widths.shelfMargin
        y: Widths.shelfGap
        spacing: 6

        Repeater {
            model: row.readStatusValues

            delegate: FilterChip {
                required property var modelData

                axis: "read"
                value: modelData.value
                label: modelData.label
                lit: row.isLit("read", modelData.value)
                onToggled: row.toggle("read", modelData.value)
            }
        }

        // The trait of the artifact: two axes, and nothing suggesting they are one list.
        Rectangle {
            objectName: "filter-separator"
            width: 1
            height: 18
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.rule
            visible: row.readStatusValues.length > 0 && row.mediumValues.length > 0
        }

        Repeater {
            model: row.mediumValues

            delegate: FilterChip {
                required property var modelData

                axis: "medium"
                value: modelData.value
                label: modelData.label
                lit: row.isLit("medium", modelData.value)
                onToggled: row.toggle("medium", modelData.value)
            }
        }

        Rectangle {
            objectName: "filter-elsewhere-separator"
            width: 1
            height: 18
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.rule
            visible: row.elsewhereValues.length > 0
                     && (row.readStatusValues.length > 0 || row.mediumValues.length > 0)
        }

        Repeater {
            model: row.elsewhereValues

            delegate: FilterChip {
                required property var modelData

                axis: modelData.axis
                value: modelData.value
                label: modelData.label
                // Always lit: it is only here because it is.
                lit: true
                onToggled: row.toggle(modelData.axis, modelData.value)
            }
        }
    }
}
