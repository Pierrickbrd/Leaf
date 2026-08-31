// The shell, and nothing else yet: the paper, the family, the width the screens read, and
// the loader the navigation drives. Each destination shows a card naming itself — enough to
// see the palette and the fonts on a real window, and not one line of a screen.

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
        sequence: StandardKey.Cancel
        onActivated: Navigation.back()
    }

    // The sourceComponent never changes yet — there is only one screen to load. It stays a
    // Loader anyway: swapping screens as the navigation changes will want exactly this seam,
    // and this is the seam the first real screen replaces instead of restructuring the shell
    // around it.
    Loader {
        anchors.fill: parent
        sourceComponent: card
    }

    Component {
        id: card

        Item {
            Column {
                anchors.centerIn: parent
                spacing: 8

                Label {
                    text: Navigation.label
                    font.family: Theme.displayFamily
                    font.pixelSize: 28
                    font.weight: Font.Bold
                    color: Theme.ink
                }

                Label {
                    text: Widths.bandLabel
                    font.family: Theme.textFamily
                    font.pixelSize: 15
                    color: Theme.inkSoft
                }
            }
        }
    }
}
