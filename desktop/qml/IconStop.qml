// A round button the keyboard reaches: `IconButton`, with a stop in the Tab order, Enter and
// Space, and the ring.
//
// `IconButton` is drawn on covers, where none of that is wanted; the pencils and the bins of
// the settings are the only way to edit a profile or remove a device, and a keyboard could
// neither reach nor press them. The reader's bar writes the same thing for its own buttons,
// at the reader's scale.

import QtQuick
import Leaf

IconButton {
    id: stop

    activeFocusOnTab: visible
    Accessible.focusable: true

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            stop.press()
            event.accepted = true
        }
    }

    FocusRing {
        cornerRadius: stop.radius
    }
}
