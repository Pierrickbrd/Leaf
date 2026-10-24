// The keys' walk through one region of a screen: every stop drawn under `region`, in the
// order it is drawn, and out at either end.
//
// A stop is an item that says so — `readonly property bool keyStop: true` — and is shown and
// enabled. It is found by walking the item tree rather than listed by hand, because the rows
// of a list come and go with their model: the series page was a list nobody could reach, and
// a list of stops written out beside it would have been one more thing for the next row to be
// missing from. A stop is not walked into: the menu inside a row is the row's, opened by the
// Menu key on it, and not a second stop in front of the next row.
//
// Tab and Down go on, Shift+Tab and Up go back; Left and Right do the same except in a field,
// where they move the caret. Past either end it says so, and where the focus goes next is the
// screen's business.

import QtQuick

QtObject {
    id: walk

    required property Item region

    /// Walked past, in that direction: forward is past the last stop.
    signal wentPast(bool forward)
    /// A stop was given the focus by a key, for a screen that scrolls to show it.
    signal reached(Item stop)

    function collect(item, into) {
        if (!item || !item.visible || !item.enabled)
            return
        if (item.keyStop === true) {
            into.push(item)
            return
        }
        // Indexed and not `for…of`: an item's children are a list property, which the Qt of
        // Ubuntu 24.04 does not iterate.
        for (let i = 0; i < item.children.length; ++i)
            collect(item.children[i], into)
    }

    function stops() {
        const all = []
        collect(walk.region, all)
        return all
    }

    function at(all) {
        for (let i = 0; i < all.length; ++i) {
            if (all[i].activeFocus)
                return i
        }
        return -1
    }

    function give(stop, forward) {
        stop.forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
        walk.reached(stop)
    }

    /// Takes the focus by the first stop going forward, the last coming back. False when there
    /// is none, which sends the focus on to whatever is past this region.
    function take(forward) {
        const all = stops()
        if (all.length === 0)
            return false
        give(all[forward ? 0 : all.length - 1], forward)
        return true
    }

    /// Answers a key pressed somewhere in the region: true when it moved the focus or left.
    function step(event) {
        const typing = event.key === Qt.Key_Left || event.key === Qt.Key_Right
        const all = stops()
        const here = at(all)
        // A field keeps Left and Right for its caret.
        if (typing && here >= 0 && all[here].cursorPosition !== undefined)
            return false
        const back = event.key === Qt.Key_Backtab || event.key === Qt.Key_Up
                || event.key === Qt.Key_Left
                || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))
        const on = !back && (event.key === Qt.Key_Tab || event.key === Qt.Key_Down
                             || event.key === Qt.Key_Right)
        if (!back && !on)
            return false
        if (here < 0)
            return take(on)
        const to = here + (on ? 1 : -1)
        if (to < 0 || to >= all.length) {
            walk.wentPast(on)
            return true
        }
        give(all[to], on)
        return true
    }
}
