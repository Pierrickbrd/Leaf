// One file of an edition, on a line.
//
// **The menu is always there**, which is what settles the layout of the rest. A gutter was
// reserved for it and left empty until the pointer crossed the row: the page count could not
// jump, but the row was drawn with two centimetres of nothing on its right and its two
// margins were not the same width — visibly so, on a wide window. Drawn always, it is the
// last thing in the row like any other, and both sides breathe the same.
//
// A rule under every line, including the last. Five volumes on a tall window were five rows
// floating in a page that did not appear to end; the rule says where the list stops.

import QtQuick
import Leaf

Item {
    id: row

    required property string entryId
    required property string number
    required property string title
    required property string pages
    required property int state
    required property real howFarRead
    required property string timesFinished
    /// « Non lu », worded in C++ like everything else on this screen.
    required property string neverReadWord
    /// What the menu in the gutter commands this file through, and the name a copy of it is
    /// offered under.
    required property string seriesId
    required property string fileName
    /// The last line of the list draws no rule: a rule under the last row is a table's
    /// bottom edge, and this is a list that ends.
    required property bool last

    readonly property bool missing: state === 3
    readonly property bool hovered: pointer.hovered || commands.opened

    signal opened()
    signal reimportAsked()

    objectName: "volume-" + (row.entryId.length > 0 ? row.entryId : "missing-" + row.number)
    implicitHeight: 46

    Accessible.role: Accessible.ListItem
    Accessible.name: row.number + " " + row.title
    Accessible.description: row.missing ? row.title : row.pages

    // Not a file, so there is nothing to open and nothing to command. A hole in a collection
    // is drawn and left alone.
    HoverHandler {
        id: pointer

        enabled: !row.missing
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        enabled: !row.missing
        onTapped: row.opened()
    }

    // The one being read wears the wash, which is how this client says « here » everywhere
    // else. Hovering is quieter than that, and the two do not fight: a hovered row that is
    // also the one in progress stays the one in progress.
    Rectangle {
        anchors.fill: parent
        anchors.bottomMargin: 1
        radius: 8
        color: row.state === 1 ? Theme.emeraldWash
                               : (row.hovered ? Theme.onPaper : "transparent")
    }

    // A hole in a collection is hatched rather than coloured: it is not a state of a file,
    // it is the absence of one, and a wash would have read as a fourth reading state.
    Canvas {
        anchors.fill: parent
        visible: row.missing
        antialiasing: true

        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.strokeStyle = Theme.inkFaint
            context.globalAlpha = 0.09
            context.lineWidth = 1
            for (let at = -height; at < width; at += 10) {
                context.beginPath()
                context.moveTo(at, height)
                context.lineTo(at + height, 0)
                context.stroke()
            }
        }
    }

    // A hair, and not under the last: thirty full-strength lines down a page would be a
    // table's grid, and what is read here is the titles. The row being read closes its own
    // wash, so it carries none either.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        visible: !row.last && row.state !== 1
        color: Theme.rule
        opacity: 0.55
    }

    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 6
        spacing: 10

        Text {
            id: no

            width: 28
            text: row.number
            color: row.missing ? Theme.inkFaint : Theme.inkSoft
            font.family: Theme.displayFamily
            font.pixelSize: 15
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignRight
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            // What is left once everything with a fixed width has taken its share: the two
            // margins, the four gaps, the number, the pages, the state and the menu.
            width: row.width - 12 - 40 - no.width - weight.width - mark.width - commands.width
            text: row.title
            color: row.missing ? Theme.alert : Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: 15
            font.italic: !row.missing && row.title.length === 0
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            id: weight

            text: row.pages
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: 13
            anchors.verticalCenter: parent.verticalCenter
        }

        Row {
            id: mark

            spacing: 5
            anchors.verticalCenter: parent.verticalCenter

            Text {
                text: row.timesFinished
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }

            // Written in full where the other two carry a ring: an empty cell at the end of a
            // line reads as an information that is missing, not as a state.
            Text {
                text: row.neverReadWord
                visible: row.state === 0
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: 13
                anchors.verticalCenter: parent.verticalCenter
            }

            VolumeState {
                visible: row.state === 1 || row.state === 2
                reading: row.state
                howFarRead: row.howFarRead
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // Last in the row, beside the state and never over it. Nothing to command on a file
        // that is not there, so a gap carries no menu — and keeps its width all the same, or
        // its own line would be laid out differently from every other.
        CommandMenu {
            id: commands

            anchors.verticalCenter: parent.verticalCenter
            opacity: row.missing ? 0 : 1
            enabled: !row.missing
            seriesId: row.seriesId
            entryId: row.entryId
            fileName: row.fileName
            label: row.title
            onReimportAsked: row.reimportAsked()
        }
    }
}
