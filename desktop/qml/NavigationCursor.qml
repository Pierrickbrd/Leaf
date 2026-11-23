// One emphasis cursor for the whole page.
//
// A screen owns the geometry of moving inside one of its regions; this object owns the fact
// that pointer and keyboard emphasis are mutually exclusive. Main creates exactly one and
// hands it to whichever screen the Loader displays.

import QtQuick

QtObject {
    signal navigationKeyRequested(Item owner, int key, int modifiers)
    signal resetRequested(Item owner)
    /// The pointer's emphasis is dropped and the keyboard's is to be shown again, where it
    /// stood: unlike a reset, the region keeps its position. Asked by Enter on its way to
    /// the focused item — see `Boot`.
    signal resumeRequested(Item owner)

    readonly property int inactiveMode: 0
    readonly property int pointerMode: 1
    readonly property int keyboardMode: 2
    property int mode: inactiveMode
    property Item region: null
    property int index: -1
    property bool entryForward: true
    property Item previousFocusItem: null
    property Item currentFocusItem: null
    /// The pointer has emphasised a cover since the keyboard last moved. Not the same as
    /// `pointerMode`, which ends when the pointer leaves the cover: the keyboard's ring stays
    /// hidden after that too, and comes back when a key moves or Enter asks for it — leaving
    /// a cover is not a decision to look at the chip the keys were left on.
    ///
    /// Set by any press in the window (`Boot`), and not only over a cover: the ring answers
    /// Tab and the arrows, and the first click puts it away. True until a key is pressed: a
    /// window that opens has been used by nobody yet, and « Qui lit ? » came up at launch with
    /// a ring round a profile nobody had walked to.
    property bool pointerTookOver: true
    /// A button is down. A focus change made under it is the hand's, and lights no ring.
    property bool pointerDown: false
    /// A key that walks — Tab, an arrow, Home, a page — is being delivered. A focus change made
    /// under it is the keys', and lights the ring.
    property bool keyDown: false

    function noteFocus(item) {
        if (currentFocusItem === item)
            return
        // A key that moved the focus lights its ring; a click that did, does not — the ring
        // is the keyboard's, and a click is the hand. A focus change that neither made is the
        // screen's, and keeps whatever the last hand or key decided: it was read as the
        // keys', so a page opened by a click, a dialog closing back onto its opener and a
        // tab chosen with the pointer all came up ringed (owner, 25/10/2026).
        if (pointerDown)
            pointerTookOver = true
        else if (keyDown)
            pointerTookOver = false
        previousFocusItem = currentFocusItem
        currentFocusItem = item
    }

    function reset() {
        const owner = region
        clear()
        resetRequested(owner)
    }

    function resumeKeyboard() {
        const owner = region
        pointerTookOver = false
        clear()
        resumeRequested(owner)
    }

    function requestNavigationKey(key, modifiers) {
        navigationKeyRequested(region, key, modifiers)
    }

    function clear(expectedRegion) {
        if (expectedRegion !== undefined && expectedRegion !== null
                && region !== expectedRegion) {
            return
        }
        mode = inactiveMode
        region = null
        index = -1
    }

    function beginKeyboard(forward) {
        pointerTookOver = false
        mode = keyboardMode
        region = null
        index = -1
        entryForward = forward
    }

    function useKeyboard(owner, position) {
        pointerTookOver = false
        mode = keyboardMode
        region = owner
        index = position
    }

    function usePointer(owner, position) {
        pointerTookOver = true
        mode = pointerMode
        region = owner
        index = position
    }
}
