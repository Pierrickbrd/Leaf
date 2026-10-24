// One file of an edition, on a cover.
//
// Exactly the shelf's tile applied to a volume: the cover, the name under it, what it weighs
// under that. A series is drawn the same way everywhere, and a volume had no reason to escape
// it — which is why the marks are the shelf's marks too.
//
// **A bar and not a ring.** A cover has a bottom edge to lay a bar along; a line of a list has
// none, and that is the whole of the difference between this and `VolumeRow`. Finished, the
// bar is full *and* a check sits in the far corner: the bar alone said two opposite things,
// never opened and done, by being absent in one case and complete in the other.
//
// Never opened carries neither, and no word either: an empty track drawn on every cover would
// be fifty grey lines on a wall of illustrations.

import QtQuick
import Leaf

Item {
    id: tile

    required property string entryId
    required property string number
    required property string title
    required property string pages
    required property string cover
    required property string fileName
    required property int state
    required property real howFarRead
    /// The edition this belongs to, which its menu commands it through.
    required property string seriesId
    required property real coverWidth
    required property real coverHeight

    readonly property bool missing: state === 3
    readonly property bool finished: state === 2
    readonly property bool started: state === 1
    readonly property bool hovered: pointer.hovered

    signal opened()
    signal reimportAsked()

    // Opening a file is reading it: the line and the cover both lead to the reader, at the
    // page where reading stopped. Here rather than where the tile is placed, so the list and
    // the grid cannot open one book two ways.
    onOpened: Navigation.open(Navigation.Reader, { "series": tile.seriesId, "entry": tile.entryId })

    objectName: "tile-volume-" + (entryId.length > 0 ? entryId : "missing-" + number)
    width: coverWidth
    height: coverHeight + 6 + 34 + 2 + 16

    Accessible.role: Accessible.ListItem
    Accessible.name: tile.number + " " + tile.title
    Accessible.description: tile.missing ? tile.title : tile.pages

    /// A stop of the page's walk — see `KeyWalk` — unless it is a hole, which opens nothing.
    readonly property bool keyStop: !tile.missing

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            tile.opened()
            event.accepted = true
        } else if (event.key === Qt.Key_Menu
                   || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            commands.show()
            event.accepted = true
        }
    }

    HoverHandler {
        id: pointer

        enabled: !tile.missing
        cursorShape: Qt.PointingHandCursor
    }

    // The cover answers the pointer. Without it the grid was a wall that did not react at
    // all: nothing said a tile could be opened, and the menu appearing in a corner was the
    // only sign anything had been noticed.
    scale: tile.hovered ? 1.03 : 1.0
    Behavior on scale {
        NumberAnimation {
            duration: 90
            easing.type: Easing.OutQuad
        }
    }

    TapHandler {
        gesturePolicy: TapHandler.ReleaseWithinBounds
        enabled: !tile.missing
        onTapped: tile.opened()
    }

    Item {
        id: frame

        width: tile.coverWidth
        height: tile.coverHeight

        CoverShadow {
            under: tile.entryId
            visible: !tile.missing
        }

        RoundedCover {
            id: clipped

            anchors.fill: parent
            source: tile.cover
            visible: !tile.missing

            ReadBar {
                key: tile.entryId
                started: tile.started
                finished: tile.finished
                howFarRead: tile.howFarRead
            }
        }

        // Nothing to show, so nothing is shown: a dashed frame in place of a cover, and the
        // word in the alert colour. A hole in a collection is drawn and left alone.
        Canvas {
            anchors.fill: parent
            visible: tile.missing
            antialiasing: true

            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.strokeStyle = Theme.rule
                context.lineWidth = 1
                context.setLineDash([4, 4])
                context.strokeRect(0.5, 0.5, width - 1, height - 1)
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 16
                text: tile.title
                color: Theme.alert
                font.family: Theme.textFamily
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }

        CoverSkin { visible: !tile.missing }

        // Around the cover, which is what the eye goes to, and lit by the tile's focus.
        FocusRing {
            objectName: tile.objectName + "-focus"
            focused: tile.activeFocus
        }

        ReadCheck {
            key: tile.entryId
            finished: tile.finished
            coverWidth: tile.coverWidth
        }

        // To the left of the check and never over it — the corner is taken, and a button does
        // not cover what it commands. On its own dark veil, because an illustration can be
        // pale and grey dots laid straight on one are invisible one time in three.
        CommandMenu {
            id: commands

            objectName: "commands-tile-" + tile.entryId
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.top: parent.top
            anchors.topMargin: 4
            coverWidth: tile.coverWidth
            visible: !tile.missing
            veiled: true
            seriesId: tile.seriesId
            entryId: tile.entryId
            fileName: tile.fileName
            label: tile.title
            onReimportAsked: tile.reimportAsked()
        }
    }

    Text {
        id: name

        anchors.left: frame.left
        anchors.right: frame.right
        anchors.top: frame.bottom
        anchors.topMargin: 6
        height: Math.min(implicitHeight, 34)
        text: tile.number + " · " + tile.title
        color: tile.missing ? Theme.inkFaint : Theme.ink
        font.family: Theme.displayFamily
        font.pixelSize: 13
        font.weight: Font.DemiBold
        lineHeightMode: Text.FixedHeight
        lineHeight: 17
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    Text {
        anchors.left: frame.left
        anchors.right: frame.right
        anchors.top: name.bottom
        anchors.topMargin: 2
        height: 16
        text: tile.pages
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: 11
        elide: Text.ElideRight
    }
}
