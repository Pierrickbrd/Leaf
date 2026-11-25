// A question asked in front of the screen: a card of words over a veil, its commands at its foot.
//
// Three dialogs drew this three times — deleting, relaying another device, unlinking this one —
// each a `Popup` 430 pixels wide, its words 17 from the left and 16 from the top, its commands
// 8 apart, its height a sum of literals. On 23/10/2026, at 800 × 560, the relaying one stood
// 550 pixels tall: flush with the window at the top and at the bottom, and with no way to
// scroll it, so a desktop text size a quarter larger cut its last lines off. This is the one
// they share now:
//
// - **its width is the words'**, `column(drawnWidth)`, and never closer to the window's edge
//   than the page's own gutter — a dialog that touches the frame reads as a second window;
// - **its height is what it says**, bounded the same way; past that its words scroll under
//   its commands, which stay where the hand expects them;
// - **its padding is a card's** (`Widths.space.card`), its lines a card's (`row`), and so are
//   the gaps between its commands — the 17 was nobody's decision.
//
// What it asks goes in it, stacked; its commands go in `commands`, right-aligned.

import QtQuick
import QtQuick.Controls
import Leaf

Popup {
    id: dialog

    /// The width it was drawn at, for a window of 1280.
    property int drawnWidth: 430
    /// The surface, for whoever asks for it by name.
    property string surfaceName: ""
    /// What it opens on, every time — see `DialogBody`.
    property Item initialFocus: null

    default property alias content: body.data
    /// Between the lines it stacks: a card's `row` unless it holds sections of its own.
    property alias lineSpacing: body.spacing
    property alias commands: commandRow.data

    /// The window's own paper left round it: the page's gutter, so no edge comes closer.
    readonly property int edge: Widths.space.gutter
    /// Whether what it says is taller than the window lets it be.
    readonly property bool scrolls: scroller.contentHeight > scroller.height
    /// A focus ring is drawn outside its control, and the field of the deletion runs the
    /// card's whole width: clipped flush, its ring lost both sides.
    readonly property int ringRoom: Theme.focusGap + 2

    width: {
        const words = Widths.scale.column(dialog.drawnWidth)
        const room = Overlay.overlay ? Overlay.overlay.width - 2 * dialog.edge : words
        return Math.max(0, Math.min(words, room))
    }
    /// What stands between its words and its commands, when it has commands.
    readonly property int beforeCommands: commandRow.height > 0 ? Widths.space.card : 0

    implicitHeight: topPadding + bottomPadding + body.implicitHeight + dialog.beforeCommands
                    + commandRow.height
    height: {
        const room = Overlay.overlay ? Overlay.overlay.height - 2 * dialog.edge
                                     : implicitHeight
        return Math.max(0, Math.min(implicitHeight, room))
    }
    anchors.centerIn: Overlay.overlay
    padding: Widths.space.card
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    // Dimmed and not hidden: what the question is about stays on the screen behind it.
    Overlay.modal: Rectangle {
        color: Qt.rgba(12 / 255, 16 / 255, 14 / 255, 0.74)
    }

    background: Rectangle {
        objectName: dialog.surfaceName
        radius: Theme.cardRadius
        // A card, not the paper: a modal the colour of the page behind it has nothing but a
        // hairline saying where the page stops and the question starts.
        color: Theme.surface
        border.color: Theme.rule
        border.width: 1
        antialiasing: true

        CardLift { level: CardLift.Modal }
    }

    contentItem: DialogBody {
        initialFocus: dialog.initialFocus

        Flickable {
            id: scroller

            objectName: dialog.objectName.length > 0 ? dialog.objectName + "-scroll" : ""
            x: -dialog.ringRoom
            y: -dialog.ringRoom
            width: parent.width + 2 * dialog.ringRoom
            height: Math.max(0, parent.height - commandRow.height - dialog.beforeCommands
                                + 2 * dialog.ringRoom)
            contentWidth: width
            contentHeight: body.implicitHeight + 2 * dialog.ringRoom
            clip: true
            interactive: dialog.scrolls
            boundsBehavior: Flickable.StopAtBounds

            ScrollIndicator.vertical: ScrollIndicator { }

            FollowsFocus {
                flick: scroller
                room: dialog.ringRoom
            }

            Column {
                id: body

                objectName: dialog.objectName.length > 0 ? dialog.objectName + "-body" : ""
                x: dialog.ringRoom
                y: dialog.ringRoom
                width: scroller.width - 2 * dialog.ringRoom
                spacing: Widths.space.row
            }
        }

        Row {
            id: commandRow

            objectName: dialog.objectName.length > 0 ? dialog.objectName + "-commands" : ""
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: Widths.space.row
        }
    }
}
