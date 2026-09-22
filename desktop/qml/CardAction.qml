// A command on a card: a pill that is not the emerald one.
//
// `ActionButton` is the emerald pill, and there is one of those per screen — « the one thing
// worth doing here ». A card of the import queue has three commands at once (accept, pause,
// abandon) and only one of them is that thing; the other two were bare coloured words
// floating at the top of the card, which read as a heading rather than as something to press.
//
// So: the same pill shape, drawn as an outline. It borrows `FilterChip`'s way of saying
// hover — a background rather than the growth `ActionButton` uses — because a row of three
// buttons that each jump under the pointer is a row that twitches.
//
// `alarming` is for a command that throws something away. Emerald says « the one thing worth
// doing »; a command that destroys is not that, and it takes the alert colour for its word
// while keeping the quiet outline, so that it is legible without competing with `Accepter`.

import QtQuick
import Leaf

Rectangle {
    id: action

    required property string label
    property bool alarming: false

    signal triggered()

    implicitWidth: word.implicitWidth + 24
    implicitHeight: 28
    width: implicitWidth
    height: implicitHeight
    radius: height / 2
    color: pointer.hovered ? Theme.rule : "transparent"
    border.color: Theme.rule
    border.width: 1
    antialiasing: true
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: action.label
    Accessible.onPressAction: action.triggered()

    Behavior on color {
        ColorAnimation { duration: 90 }
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            action.triggered()
            event.accepted = true
        }
    }

    HoverHandler {
        id: pointer

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: action.triggered()
    }

    Text {
        id: word

        anchors.centerIn: parent
        text: action.label
        color: action.alarming ? Theme.alert : Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: 13
        font.weight: Font.Medium
    }

    FocusRing {
        cornerRadius: action.radius
    }
}
