// One emphasis cursor for the whole page.
//
// A screen owns the geometry of moving inside one of its regions; this object owns the fact
// that pointer and keyboard emphasis are mutually exclusive. Main creates exactly one and
// hands it to whichever screen the Loader displays.

import QtQuick

QtObject {
    signal navigationKeyRequested(Item owner, int key, int modifiers)
    signal resetRequested(Item owner)

    readonly property int inactiveMode: 0
    readonly property int pointerMode: 1
    readonly property int keyboardMode: 2
    property int mode: inactiveMode
    property Item region: null
    property int index: -1
    property bool entryForward: true
    property Item previousFocusItem: null
    property Item currentFocusItem: null

    function noteFocus(item) {
        if (currentFocusItem === item)
            return
        previousFocusItem = currentFocusItem
        currentFocusItem = item
    }

    function reset() {
        const owner = region
        clear()
        resetRequested(owner)
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
        mode = keyboardMode
        region = null
        index = -1
        entryForward = forward
    }

    function useKeyboard(owner, position) {
        mode = keyboardMode
        region = owner
        index = position
    }

    function usePointer(owner, position) {
        mode = pointerMode
        region = owner
        index = position
    }
}
