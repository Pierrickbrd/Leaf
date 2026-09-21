// One pill of the row, and the three states it can be in.
//
// The only visual state that matters on a chip is **lit or not** — which is the artifact's
// argument for refusing a colour per value: if "terminées" were always green, that green would
// mean nothing when the chip is off, and the one thing worth seeing would become the harder
// one to see. So the emerald here says *selected*, and never *what*.
//
// Hover takes a background rather than the growth `ActionButton` uses: a chip already has one,
// and a row of six pills that each jump under the pointer is a row that twitches. The step is
// `rule` in both themes on purpose — `onBar` and `onPaper` are the same colour in the dark, so
// a hover drawn with it would be invisible on exactly one of the two.

import QtQuick
import Leaf

Rectangle {
    id: chip

    /// "read" or "medium", handed back with the value so the row knows which list to change.
    required property string axis
    /// The contract's spelling, which goes back on the wire untouched.
    required property string value
    /// « Non lues 12 », worded and counted in C++.
    required property string label
    required property bool lit

    signal toggled()

    readonly property bool hovered: pointer.hovered

    objectName: "chip-" + value
    implicitWidth: word.implicitWidth + 2 * 14
    width: implicitWidth
    height: 28
    radius: height / 2
    color: lit ? Theme.emeraldWash : (hovered ? Theme.rule : Theme.onPaper)
    border.color: lit ? Theme.emerald : "transparent"
    border.width: 1
    antialiasing: true
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

    HoverHandler {
        id: pointer

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: chip.toggled()
    }

    FocusRing {
        objectName: chip.objectName + "-focus"
        cornerRadius: chip.height / 2
    }

    Text {
        id: word

        objectName: chip.objectName + "-text"
        anchors.centerIn: parent
        text: chip.label
        color: chip.lit ? Theme.emerald : Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: 14
        font.weight: chip.lit ? Font.Medium : Font.Normal
    }
}
