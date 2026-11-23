// One bubble: what happened, in two lines, with at most one thing to do about it.
//
// The colour is on the icon and nowhere else. The card keeps a card's background, because a
// bubble filled with colour on a wall of covers would be the flashing light the architecture
// already refuses for pills — and the icon alone is enough to tell the two tones apart at the
// distance a bubble is read from.
//
// The cross is not one more action. It is the way out, and the two are not counted together:
// without it « click to close » would be an instruction nobody can see. Every bubble goes by
// itself after ten seconds, and its time stops while the pointer is on it (`Toasts.hold`).

import QtQuick
import Leaf

Rectangle {
    id: bubble

    required property int index
    required property int tone
    required property string headline
    required property string detail
    required property string label
    required property int offer

    readonly property bool failed: tone === 1

    signal acted()
    signal dismissed()
    /// The pointer came onto the bubble, or left it.
    signal held(bool on)

    objectName: "toast-" + index
    /// Whether the keys are on the bubble or on one of its two buttons. Its own `activeFocus`
    /// said neither: a rectangle is not a scope, and the keys on its cross left the ten
    /// seconds running until it went with the focus.
    readonly property bool keysInside: {
        for (let at = Window.activeFocusItem; at; at = at.parent) {
            if (at === bubble)
                return true
        }
        return false
    }
    // Its time stops while the keys are on it, as it does under the pointer, and goes on
    // only once both have left: a bubble that went while its cross was being reached took the
    // focus with it.
    readonly property bool holding: bubble.keysInside || pointer.hovered
    onHoldingChanged: bubble.held(bubble.holding)
    // The stack's width, which bounds it: see `ToastStack`.
    width: parent ? parent.width : Widths.scale.column(420)
    // From its words, `compact` above and under them, and never under a line's worth of
    // bubble: two lines of a larger desktop text no longer run out of a literal 52.
    implicitHeight: Math.max(Widths.scale.size(52), words.implicitHeight + 2 * Widths.space.compact)
    height: implicitHeight
    radius: Theme.cardRadius
    color: Theme.surface
    border.color: Theme.rule
    border.width: 1
    antialiasing: true

    Accessible.role: Accessible.Notification
    Accessible.name: bubble.headline
    Accessible.description: bubble.detail

    CardLift { level: CardLift.Bubble }

    // Being read is not being ignored: the ten seconds wait while the pointer is here.
    HoverHandler {
        id: pointer
    }

    Glyph {
        id: mark

        x: Widths.space.card
        anchors.verticalCenter: parent.verticalCenter
        side: Widths.scale.icon(20)
        source: bubble.failed ? "assets/icons/error.svg" : "assets/icons/check_circle.svg"
        tint: bubble.failed ? Theme.alert : Theme.emerald
    }

    // One row, everything on its middle: the icon, what happened, the one thing offered and
    // the way out. Stacked in a column the action sat under the words and the cross floated
    // in a corner of its own, which is two more places to look than a bubble is worth.
    Column {
        id: words

        anchors.left: mark.right
        anchors.leftMargin: Widths.space.label
        anchors.right: act.left
        anchors.rightMargin: Widths.space.row
        anchors.verticalCenter: parent.verticalCenter
        spacing: Widths.space.tight

        Text {
            objectName: bubble.objectName + "-headline"
            width: parent.width
            textFormat: Text.PlainText
            text: bubble.headline
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            textFormat: Text.PlainText
            text: bubble.detail
            visible: text.length > 0
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(11)
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }

    // One at most, and none at all for what has nothing to propose: an « OK » that only
    // closes is the cross with a word written on it.
    LeafTextAction {
        id: act

        objectName: bubble.objectName + "-action"
        anchors.right: cross.left
        anchors.rightMargin: Widths.scale.room(4)
        anchors.verticalCenter: parent.verticalCenter
        scaled: true
        label: bubble.label
        visible: bubble.offer !== 0
        width: visible ? implicitWidth : 0
        onTriggered: bubble.acted()
    }

    IconButton {
        id: cross

        objectName: bubble.objectName + "-close"
        anchors.right: parent.right
        anchors.rightMargin: Widths.space.compact
        anchors.verticalCenter: parent.verticalCenter
        side: Widths.scale.icon(16)
        glyph: "close"
        label: bubble.headline
        tint: Theme.inkFaint
        onTriggered: bubble.dismissed()
    }
}
