// One family of event, and the two channels it may take.
//
// A line and not a card: four lines and two columns of switches fit in one card, where four
// cards of three pills would have filled the section to say the same thing — and a line makes
// room for a fifth family the day something asks for one, without redrawing anything.

import QtQuick
import Leaf

Item {
    id: family

    required property string label
    required property string detail
    required property bool bubble
    required property bool desktop
    /// Where the two columns of switches begin, so every line's switches line up whatever the
    /// words beside them are, and how wide each of the two is.
    required property real switches
    readonly property real column: 54

    signal bubbleAsked(bool wanted)
    signal desktopAsked(bool wanted)

    implicitHeight: Math.max(words.implicitHeight + 14, 40)
    height: implicitHeight

    // Over each line rather than under it: the last line of the card closes on the card's own
    // edge, and a rule under it would draw a second one a hair away.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.rule
        opacity: 0.55
    }

    Column {
        id: words

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.rightMargin: family.width - family.switches + 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Text {
            width: parent.width
            text: family.label
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: 14
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            text: family.detail
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }

    // Centred in their own column, which is what each heading is centred over. Placed at
    // « the width less a hundred and twenty », the columns wandered with the window and
    // neither switch ever sat under its own heading.
    LeafSwitch {
        objectName: family.objectName + "-bubble"
        x: family.switches + (family.column - width) / 2
        anchors.verticalCenter: parent.verticalCenter
        label: family.label
        on: family.bubble
        onToggled: wanted => family.bubbleAsked(wanted)
    }

    LeafSwitch {
        objectName: family.objectName + "-desktop"
        x: family.switches + family.column + (family.column - width) / 2
        anchors.verticalCenter: parent.verticalCenter
        label: family.label
        on: family.desktop
        onToggled: wanted => family.desktopAsked(wanted)
    }
}
