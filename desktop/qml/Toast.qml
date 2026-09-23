// One bubble: what happened, in two lines, with at most one thing to do about it.
//
// The colour is on the icon and nowhere else. The card keeps a card's background, because a
// bubble filled with colour on a wall of covers would be the flashing light the architecture
// already refuses for pills — and the icon alone is enough to tell the two tones apart at the
// distance a bubble is read from.
//
// The cross is not one more action. It is the way out, and the two are not counted together:
// without it « click to close » would be an instruction nobody can see, and a failure stays
// up until exactly that click.

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

    objectName: "toast-" + index
    width: 420
    implicitHeight: Math.max(52, words.implicitHeight + 22)
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

    Glyph {
        id: mark

        x: 14
        anchors.verticalCenter: parent.verticalCenter
        side: 20
        source: bubble.failed ? "assets/icons/error.svg" : "assets/icons/check_circle.svg"
        tint: bubble.failed ? Theme.alert : Theme.emerald
    }

    // One row, everything on its middle: the icon, what happened, the one thing offered and
    // the way out. Stacked in a column the action sat under the words and the cross floated
    // in a corner of its own, which is two more places to look than a bubble is worth.
    Column {
        id: words

        anchors.left: mark.right
        anchors.leftMargin: 12
        anchors.right: act.left
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1

        Text {
            objectName: bubble.objectName + "-headline"
            width: parent.width
            text: bubble.headline
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            text: bubble.detail
            visible: text.length > 0
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: 11
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
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        label: bubble.label
        visible: bubble.offer !== 0
        width: visible ? implicitWidth : 0
        onTriggered: bubble.acted()
    }

    IconButton {
        id: cross

        objectName: bubble.objectName + "-close"
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        side: 16
        glyph: "close"
        label: bubble.headline
        tint: Theme.inkFaint
        onTriggered: bubble.dismissed()
    }
}
