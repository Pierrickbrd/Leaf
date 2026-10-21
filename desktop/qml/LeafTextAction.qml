// A command written as a word rather than drawn as a button.
//
// « Tout effacer » is not a destination and not a choice among others: giving it a button's
// weight would make it compete with the axes it sits above. It earns emphasis by being the
// one emerald thing in a heading otherwise written in faint ink.

import QtQuick
import Leaf

Item {
    id: action

    required property string label
    /// Drawn in the alert colour, for a command that throws something away. Emerald says
    /// "the one thing worth doing here"; a command that destroys is not that.
    property bool alarming: false
    /// Written in soft ink instead of emerald, for a link that must not compete with the
    /// filled button beside it (the end card's two ways out).
    property bool quiet: false
    /// Whether it follows the window (`Widths.scale`): only on the reader's end card yet, so
    /// that its two ways out follow the card round them.
    property bool scaled: false

    function sized(drawn: real): int {
        return action.scaled ? Widths.scale.size(drawn) : drawn
    }

    signal triggered()

    objectName: "leaf-text-action"
    implicitWidth: word.implicitWidth + action.sized(16)
    implicitHeight: action.sized(24)
    width: implicitWidth
    height: implicitHeight
    activeFocusOnTab: false

    Accessible.role: Accessible.Button
    Accessible.name: action.label
    Accessible.onPressAction: action.triggered()

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
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: action.triggered()
    }

    Rectangle {
        anchors.fill: parent
        radius: action.sized(6)
        color: pointer.hovered ? Theme.onPaper : "transparent"
    }

    Text {
        id: word

        objectName: "leaf-text-action-word"
        anchors.centerIn: parent
        text: action.label
        color: action.alarming ? Theme.alert : (action.quiet ? Theme.inkSoft : Theme.emerald)
        font.family: Theme.textFamily
        font.pixelSize: action.scaled ? Widths.scale.caption(13) : 13
        font.weight: Font.Medium
    }

    FocusRing {
        objectName: "leaf-text-action-focus"
        cornerRadius: action.sized(6)
    }
}
