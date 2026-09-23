// One pill of the row, and the three states it can be in.
//
// The only visual state that matters on a chip is **lit or not** — which is the artifact's
// argument for refusing a colour per value: if "terminées" were always green, that green would
// mean nothing when the chip is off, and the one thing worth seeing would become the harder
// one to see. So the emerald here says *selected*, and never *what*.
//
// Hover takes a colour rather than the growth `ActionButton` uses: a row of six pills that each
// jump under the pointer is a row that twitches. Which colour is `Pill`'s, shared with the
// genres of a series page.

import QtQuick
import Leaf

Pill {
    id: chip

    /// "read" or "medium", handed back with the value so the row knows which list to change.
    required property string axis
    /// The contract's spelling, which goes back on the wire untouched.
    required property string value
    // `label` — « Non lues 12 », worded and counted in C++ — and `lit` are `Pill`'s.

    signal toggled()

    objectName: "chip-" + value
    height: 28
    onPressed: chip.toggled()
    /// A stop of the row, and the row is walked by the screen rather than by Qt's own
    /// traversal — see ShelfView. Out of that traversal on purpose: in it, the row sits after
    /// the covers whichever way it is declared, and Tab from the last cover came back to the
    /// chips instead of leaving for the bar. Nothing else here marks a chip.
    readonly property bool filterChip: true
    activeFocusOnTab: false

    Accessible.role: Accessible.Button
    Accessible.name: chip.label
    Accessible.checkable: true
    Accessible.checked: chip.lit
    Accessible.focusable: true
    Accessible.focused: chip.activeFocus
    Accessible.onPressAction: chip.toggled()

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            chip.toggled()
            event.accepted = true
        }
    }

    FocusRing {
        objectName: chip.objectName + "-focus"
        cornerRadius: chip.height / 2
    }
}
