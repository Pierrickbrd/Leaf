// A command in the application bar: no permanent frame, one hover surface, one focus ring.

import QtQuick
import QtQuick.Controls
import Qt5Compat.GraphicalEffects
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

    implicitWidth: 12 + 24 + (value.length > 0 ? 7 + word.implicitWidth : 0) + 12
    width: implicitWidth
    height: 34
    radius: Theme.buttonRadius
    color: held ? Theme.emeraldWash : (hovered ? Theme.onPaper : "transparent")
    /// Out of Qt's own traversal: the bar walks its stops itself, and the page walks from the
    /// bar into the screen below and back round. That order follows what the eye reads, which
    /// the item tree cannot express — see AppBar and ShelfView.
    activeFocusOnTab: false

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

    Image {
        id: glyph

        x: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 24
        height: 24
        source: button.source
        sourceSize: Qt.size(24, 24)
        visible: false
    }

    ColorOverlay {
        anchors.fill: glyph
        source: glyph
        color: button.held ? Theme.emerald : Theme.inkSoft
    }

    Text {
        id: word

        anchors.left: glyph.right
        anchors.leftMargin: 7
        anchors.verticalCenter: parent.verticalCenter
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
