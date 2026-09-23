// A round icon button, sized by its icon rather than by a row of them.
//
// `BarButton` is the bar's button: it lays its glyph at twelve pixels from the left because a
// bar button may carry a word after it, and its height is the bar's. Forced into a square the
// size of a postage stamp — which is what the three dots on a cover are — the glyph fell
// outside the button it belonged to, and what was left looked broken because it was.

import QtQuick
import Leaf

Rectangle {
    id: button

    /// The icon's file name, without its path.
    required property string glyph
    /// What it does, for whoever reads the screen aloud. Never drawn.
    required property string label
    property int side: 20
    property color tint: Theme.inkSoft
    /// Drawn as though it were pressed in, for a button whose popup is open.
    property bool held: false
    /// The popup it opens, so that no caller can get the gesture wrong — the same care
    /// `BarButton` takes, and for the same reason: a press outside a popup closes it, and a
    /// button deciding on the release then finds it closed and opens it again.
    property var popup: null
    property bool wasOpen: false

    readonly property bool hovered: pointer.hovered

    signal triggered()

    width: 26
    height: 26
    radius: width / 2
    color: held ? Theme.onPaper : (hovered ? Theme.onPaper : "transparent")

    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.onPressAction: button.press()

    function press(open) {
        if (popup) {
            const up = open === undefined ? popup.opened : open
            if (up)
                popup.close()
            else
                popup.open()
        }
        button.triggered()
    }

    HoverHandler {
        id: pointer

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onPressedChanged: {
            if (pressed)
                button.wasOpen = button.popup ? button.popup.opened : false
        }
        onTapped: button.press(button.wasOpen)
    }

    Glyph {
        anchors.centerIn: parent
        side: button.side
        source: "assets/icons/" + button.glyph + ".svg"
        tint: button.tint
    }
}
