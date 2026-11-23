// A round button the keyboard reaches: `IconButton`, with a stop in the Tab order.
//
// `IconButton` is drawn on covers, where none of that is wanted; the pencils and the bins of
// the settings are the only way to edit a profile or remove a device, and a keyboard could
// neither reach nor press them. Enter, Space and the ring are `IconButton`'s own: written here
// a second time, both handlers answered every key, and Space on « + » added two years
// (audit, 25/10/2026).

import QtQuick
import Leaf

IconButton {
    activeFocusOnTab: visible
    Accessible.focusable: true
}
