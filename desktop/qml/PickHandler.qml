// Ctrl+click and Shift+click: the pointer's way of picking without opening.
//
// Beside the plain tap, which answers only the click with no modifier: both handlers
// answering one click would open the page of a series the reader meant to pick.

import QtQuick

Item {
    id: pick

    signal toggled()

    anchors.fill: parent

    TapHandler {
        acceptedModifiers: Qt.ControlModifier
        gesturePolicy: TapHandler.ReleaseWithinBounds
        enabled: pick.enabled
        onTapped: pick.toggled()
    }

    TapHandler {
        acceptedModifiers: Qt.ShiftModifier
        gesturePolicy: TapHandler.ReleaseWithinBounds
        enabled: pick.enabled
        onTapped: pick.toggled()
    }
}
