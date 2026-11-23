// One choice on a sheet of the checking frame, as a box with its radio mark: a reading of a
// folder, or where an archive at a universe's root goes. The one in force wears the emerald
// on its wash; one that cannot be chosen here is greyed and says why on its second line.
//
// Its own file because the sheet draws two lists of them, the readings and the places, and the
// second copy of a radio box is the first one to drift.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Rectangle {
    id: choice

    required property string label
    /// The line under the label: what the choice makes of the drop, or why it is greyed.
    property string detail: ""
    /// A word at its right end — « Plus probable », « Nom à donner » — and its look, a
    /// `Review::Origin`.
    property string badge: ""
    property int badgeTone: Review.Guessed
    property bool chosen: false
    property bool possible: true
    /// The frame is a look at what already went: nothing chosen any more.
    property bool readOnly: false

    signal picked()

    readonly property bool takes: choice.possible && !choice.readOnly

    implicitHeight: Math.max(Widths.scale.fit(48), words.implicitHeight + 2 * Widths.space.row)
    radius: Widths.scale.size(12)
    color: choice.chosen ? Theme.emeraldWash : Theme.onPaper
    border.color: choice.chosen ? Theme.emerald : Theme.rule
    border.width: choice.chosen ? 2 : 1
    activeFocusOnTab: choice.takes

    Accessible.role: Accessible.RadioButton
    Accessible.name: choice.label
    Accessible.description: choice.detail
    Accessible.checkable: true
    Accessible.checked: choice.chosen
    Accessible.onPressAction: choice.pick()

    function pick() {
        if (choice.takes && !choice.chosen)
            choice.picked()
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            choice.pick()
            event.accepted = true
        }
    }

    HoverHandler {
        id: pointer

        enabled: choice.takes && !choice.chosen
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        enabled: choice.takes
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: choice.pick()
    }

    Rectangle {
        id: mark

        x: Widths.space.row
        // On the middle of the first line of the name, wherever the words settled: they are
        // centred in the card, and a card with no detail line is the 48 floor, where the dot
        // set from the top stood six pixels above its own word.
        y: Math.round(words.y + name.y + (firstLine.height - height) / 2)
        width: Widths.scale.icon(16)
        height: width
        radius: width / 2
        color: "transparent"
        border.width: choice.chosen ? Math.round(width * 5 / 16) : 2
        border.color: choice.chosen ? Theme.emerald
                    : pointer.hovered ? Theme.inkSoft : Theme.inkFaint
    }

    FontMetrics {
        id: firstLine

        font: name.font
    }

    Column {
        id: words

        anchors.left: mark.right
        anchors.leftMargin: Widths.space.row
        anchors.right: tag.visible ? tag.left : parent.right
        anchors.rightMargin: Widths.space.row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Text {
            id: name

            objectName: choice.objectName + "-label"
            width: parent.width
            text: choice.label
            textFormat: Text.PlainText
            color: choice.possible ? Theme.ink : Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }

        Text {
            objectName: choice.objectName + "-detail"
            width: parent.width
            visible: text.length > 0
            text: choice.detail
            textFormat: Text.PlainText
            color: choice.possible ? Theme.inkSoft : Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            wrapMode: Text.Wrap
        }
    }

    OriginChip {
        id: tag

        objectName: choice.objectName + "-badge"
        anchors.right: parent.right
        anchors.rightMargin: Widths.space.row
        // The same line as the dot and the name: its middle, not its top.
        y: Math.round(words.y + name.y + (firstLine.height - height) / 2)
        visible: choice.badge.length > 0
        origin: choice.badgeTone
        label: choice.badge
    }

    FocusRing {
        objectName: choice.objectName + "-focus"
        cornerRadius: choice.radius
    }
}
