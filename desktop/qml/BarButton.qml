// A command in the application bar: no permanent frame, one hover surface, one focus ring.

import QtQuick
import QtQuick.Controls
import Leaf

Rectangle {
    id: button

    required property url source
    required property string label
    property string value
    property bool held: false
    property bool toolTipSuppressed: false
    /// The popup this button opens, when it opens one. Given here rather than toggled by the
    /// caller so that no button can get the gesture wrong.
    property var popup: null
    /// Whether the popup was open when the press began.
    ///
    /// Read at the press and not at the release, because between the two the popup may have
    /// closed itself: a press outside a popup shuts it, and a button that decides on the
    /// release then finds it closed and opens it again — one gesture, and the popup never
    /// appears to shut. Asking at the press makes the answer independent of which of the
    /// two halves the popup happened to act on, and of any timing.
    property bool popupWasOpen: false

    signal triggered()
    signal pointerEntered()

    readonly property bool hovered: pointer.hovered

    // A `compact` padding either side, as every small surface that opens or acts has; the
    // glyph a `label` from its word, as in every control. The 7 here was the only 7 in the
    // application (spacing audit, 23/10/2026). The box, not the glyph, stands at the gutter:
    // the hover surface is what lines up with the page, as the mark does.
    implicitWidth: Widths.space.compact + glyph.width
                   + (value.length > 0 ? Widths.space.label + word.implicitWidth : 0)
                   + Widths.space.compact
    width: implicitWidth
    height: Widths.scale.size(34)
    radius: Theme.buttonRadius
    color: held ? Theme.emeraldWash : (hovered ? Theme.onPaper : "transparent")
    /// Out of Qt's own traversal: the bar walks its stops itself, and the page walks from the
    /// bar into the screen below and back round. That order follows what the eye reads, which
    /// the item tree cannot express — see AppBar and ShelfView.
    activeFocusOnTab: false
    /// A stop of a screen that walks its stops with `KeyWalk` — the way back of a series page.
    readonly property bool keyStop: true

    Accessible.role: Accessible.Button
    Accessible.name: label
    Accessible.focusable: true
    Accessible.focused: activeFocus
    Accessible.onPressAction: button.press()

    /// What every way of pressing this button goes through. `wasOpen` is what the popup was
    /// doing when the gesture started; left out, it is asked for now, which is right for a
    /// key press since a key cannot have closed anything on its way down.
    function press(wasOpen) {
        if (popup) {
            const open = wasOpen === undefined ? popup.opened : wasOpen
            if (open)
                popup.close()
            else
                popup.open()
        }
        button.triggered()
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            button.press()
            event.accepted = true
        }
    }

    HoverHandler {
        id: pointer

        cursorShape: Qt.PointingHandCursor
        onHoveredChanged: {
            if (hovered)
                button.pointerEntered()
        }
    }

    TapHandler {
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onPressedChanged: {
            if (pressed)
                button.popupWasOpen = button.popup ? button.popup.opened : false
        }
        onTapped: button.press(button.popupWasOpen)
    }

    FocusRing {
        objectName: button.objectName + "-focus"
        cornerRadius: button.radius
    }

    Glyph {
        id: glyph

        x: Widths.space.compact
        anchors.verticalCenter: parent.verticalCenter
        side: Widths.scale.icon(24)
        source: button.source
        tint: button.held ? Theme.emerald : Theme.inkSoft
    }

    Text {
        id: word

        anchors.left: glyph.right
        anchors.leftMargin: Widths.space.label
        anchors.verticalCenter: parent.verticalCenter
        textFormat: Text.PlainText
        text: button.value
        visible: text.length > 0
        color: button.held ? Theme.emerald : Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: 14
        font.weight: Font.Medium
    }

    LeafToolTip {
        objectName: button.objectName + "-tooltip"
        visible: button.hovered && !button.toolTipSuppressed
        text: button.label
    }
}
