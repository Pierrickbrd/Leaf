// On or off, drawn as a switch.
//
// Beside `LeafCheck` and not instead of it: a box says « choose this one », a switch says
// « this is on ». Four families of event, each either warning or not, are the second — and a
// column of checkboxes would have read as a list to pick from rather than as four things
// turned on.
//
// The travel is the whole of it: the knob moves, the track fills, and neither is a colour on
// its own. A switch that only changed colour would say nothing to a reader who cannot tell
// this emerald from this grey.

import QtQuick
import Leaf

Item {
    id: lever

    /// What it turns on, for whoever reads the screen aloud. Never drawn — the line beside it
    /// carries the words, and a switch with its own label in a table would say it twice.
    required property string label
    property bool on: false

    signal toggled(bool wanted)

    objectName: "leaf-switch"
    implicitWidth: 34
    implicitHeight: 19
    width: implicitWidth
    height: implicitHeight
    activeFocusOnTab: true

    Accessible.role: Accessible.CheckBox
    Accessible.name: lever.label
    Accessible.checkable: true
    Accessible.checked: lever.on
    Accessible.onPressAction: lever.toggled(!lever.on)

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            lever.toggled(!lever.on)
            event.accepted = true
        }
    }

    HoverHandler {
        id: pointer

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: lever.toggled(!lever.on)
    }

    Rectangle {
        id: track

        anchors.fill: parent
        radius: height / 2
        // A wash and a ring rather than a slab of emerald: it is how a chosen pill is drawn
        // everywhere else in this client, and what moves is the knob.
        color: lever.on ? Theme.emeraldWash : Theme.onPaper
        border.color: lever.on ? Theme.emerald
                               : (pointer.hovered ? Theme.inkFaint : Theme.rule)
        border.width: 1
        antialiasing: true

        Behavior on color {
            ColorAnimation { duration: 110 }
        }
    }

    Rectangle {
        id: knob

        objectName: lever.objectName + "-knob"
        y: 3
        x: lever.on ? lever.width - width - 3 : 3
        width: 13
        height: 13
        radius: height / 2
        color: lever.on ? Theme.emerald : Theme.inkFaint
        antialiasing: true

        Behavior on x {
            NumberAnimation {
                duration: 110
                easing.type: Easing.OutQuad
            }
        }
    }

    FocusRing {
        objectName: lever.objectName + "-focus"
        cornerRadius: lever.height / 2
        visible: lever.activeFocus
    }
}
