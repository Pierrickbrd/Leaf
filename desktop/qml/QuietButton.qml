// The outlined pill that stands beside `ActionButton`: the second way in, never the first.
//
// Two doors in the drop zone — a folder, files — are two buttons of one size, one emerald and
// one quiet, and not a button and a link: `LeafTextAction` is a bare word, and beside the
// emerald pill it read as a caption rather than as something that opens a picker. `CardAction`
// is the same outline at a card's size; this one takes the pill's own, so that the two line up.

import QtQuick
import Leaf

Rectangle {
    id: button

    required property string label

    /// Follows the window, as `ActionButton`'s does: the two stand side by side.
    property bool scaled: false

    signal triggered()

    function sized(drawn: real): int {
        return button.scaled ? Widths.scale.fit(drawn) : drawn
    }

    implicitWidth: word.implicitWidth + button.sized(40)
    height: button.sized(32)
    width: implicitWidth
    radius: height / 2
    color: pointer.hovered && button.enabled ? Theme.rule : Theme.surface
    border.color: Theme.rule
    border.width: 1
    antialiasing: true
    activeFocusOnTab: visible
    readonly property bool keyStop: true

    Accessible.role: Accessible.Button
    Accessible.name: button.label
    Accessible.focusable: true
    Accessible.focused: button.activeFocus
    Accessible.onPressAction: button.triggered()

    HoverHandler {
        id: pointer

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: button.triggered()
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            if (!event.isAutoRepeat)
                button.triggered()
            event.accepted = true
        }
    }

    FocusRing {
        objectName: button.objectName + "-focus"
        cornerRadius: button.height / 2
    }

    Text {
        id: word

        objectName: button.objectName + "-text"
        anchors.centerIn: parent
        text: button.label
        textFormat: Text.PlainText
        // Faint when it cannot answer, as `CardAction`'s is: it read as a button that did nothing.
        color: button.enabled ? Theme.ink : Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: button.scaled ? Widths.scale.body(14) : 14
        font.weight: Font.Medium
    }
}
