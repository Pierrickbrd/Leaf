// The emerald pill: one shape, one set of states, for every screen's main action.
//
// It is a file rather than a block inside ResumeBand because the second one of these — on a
// series page, in the reader — must behave identically without anyone remembering to copy the
// behaviour. What is shared is the shape, the two tones, and the two states below; what
// differs is the word and the glyph, which the caller supplies.
//
// The two states say different things and must therefore look different:
//
//   · **hover** is the pointer saying "this one". The pill is already filled with emerald, so
//     it cannot take a background the way the bar's icon buttons will — it grows instead;
//   · **focus** is the keyboard saying where it stands, and that is the ring, which never
//     touches what it rings: the gap puts it on the card where the emerald's contrast is the
//     measured one, rather than on the emerald itself where it would vanish.
//
// The glyph goes in as a child — `ActionButton { Canvas { … } }` — because the client embeds
// two text families and neither carries an icon set. A glyph borrowed from whatever the
// desktop has installed is a different shape on every machine.

import QtQuick
import Leaf

Rectangle {
    id: pill

    /// The word. Absent from the pill when `compact`, never absent from the accessibility.
    property string label
    /// Under 1100 px the word goes and the glyph stays, and the pill becomes a circle.
    property bool compact: false

    default property alias glyph: glyphs.data

    /// What it does. Given here rather than left to each caller's own `TapHandler`, so that
    /// a keyboard press and a pointer press cannot end up meaning two different things.
    signal triggered()

    /// A command that cannot do anything yet says so rather than doing nothing when
    /// pressed — « Importer » with a question still unanswered is exactly that.
    property bool ready: true

    /// Painted in the alert colour rather than the emerald. For the one button in this client
    /// that unmakes files: everything else about it is the same pill, so it is a colour and
    /// not a second component.
    property bool danger: false

    readonly property bool hovered: pointer.hovered

    height: 32
    width: compact ? height : 14 + glyphs.width + 8 + word.implicitWidth + 14
    radius: height / 2
    color: !ready ? Theme.rule : (danger ? Theme.alert : Theme.emerald)
    opacity: ready ? 1.0 : 0.7
    activeFocusOnTab: visible

    // Six per cent: enough to be felt under the pointer, small enough that a button beside a
    // cover does not appear to jump when the pointer crosses it.
    scale: hovered && ready ? 1.06 : 1.0
    Behavior on scale {
        NumberAnimation {
            duration: 90
            easing.type: Easing.OutQuad
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: pill.label
    Accessible.focusable: true
    Accessible.focused: pill.activeFocus
    Accessible.onPressAction: pill.triggered()

    HoverHandler {
        id: pointer

        cursorShape: pill.ready ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    TapHandler {
        enabled: pill.ready
        onTapped: pill.triggered()
    }

    Keys.onPressed: event => {
        if (!pill.ready)
            return
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            pill.triggered()
            event.accepted = true
        }
    }

    FocusRing {
        objectName: pill.objectName + "-focus"
        cornerRadius: pill.height / 2
    }

    Item {
        id: glyphs

        objectName: pill.objectName + "-glyph"
        width: childrenRect.width
        height: childrenRect.height
        x: pill.compact ? Math.round((pill.width - width) / 2) : 14
        anchors.verticalCenter: parent.verticalCenter
    }

    Text {
        id: word

        objectName: pill.objectName + "-text"
        anchors.left: glyphs.right
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: pill.label
        color: Theme.onEmerald
        font.family: Theme.textFamily
        font.pixelSize: 14
        font.weight: Font.Medium
        visible: !pill.compact
    }
}
