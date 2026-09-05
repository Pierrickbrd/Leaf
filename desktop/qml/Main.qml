// The shell: the paper, the window width shared with every screen, and the Loader that
// navigation will drive. The shelf is its first real destination; later screens replace only
// the Loader's source rather than restructuring the window around themselves.

import QtQuick
import QtQuick.Controls
import Leaf

ApplicationWindow {
    id: window

    width: 1100
    height: 760
    visible: true
    title: qsTr("Leaf")

    color: Theme.paper

    // The three widths are declared once, in Widths, and fed from here — the one place that
    // knows how wide the window is.
    onWidthChanged: Widths.window = window.width
    Component.onCompleted: Widths.window = window.width

    // Escape comes back. It is here rather than on each screen so that a screen written later
    // cannot forget it.
    Shortcut {
        // StandardKey is a global QML enumeration: there is deliberately no object to qualify.
        // qmllint disable unqualified
        sequence: StandardKey.Cancel
        // qmllint enable unqualified
        onActivated: Navigation.back()
    }

    Loader {
        anchors.fill: parent
        sourceComponent: shelfScreen
    }

    Component {
        id: shelfScreen

        ShelfView {}
    }
}
