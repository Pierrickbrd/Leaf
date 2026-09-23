// One file of an edition, on a line.
//
// The gutter on the right is reserved on **every** row and stays empty until the pointer is
// over one. Laid out the other way, the « … » would either cover the state it commands or
// make the page count and the ring jump left under the cursor on every row it crosses.

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

    readonly property bool missing: state === 3
    readonly property bool hovered: pointer.hovered || commands.opened

    signal opened()
    signal reimportAsked()

    objectName: "volume-" + (row.entryId.length > 0 ? row.entryId : "missing-" + row.number)
    implicitHeight: 34

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

    Rectangle {
        anchors.fill: parent
        anchors.bottomMargin: 1
        radius: 8
        color: row.hovered ? Theme.onPaper : "transparent"
    }

    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        // The reserved gutter. Two centimetres of nothing cost less than a page count that
        // moves under the pointer.
        anchors.rightMargin: 30
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 6
        spacing: 10

        Text {
            id: no

            width: 24
            text: row.number
            color: row.missing ? Theme.inkFaint : Theme.inkSoft
            font.family: Theme.displayFamily
            font.pixelSize: 13
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignRight
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            width: row.width - 30 - no.width - weight.width - mark.width - 46
            text: row.title
            color: row.missing ? Theme.alert : Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: 13
            font.italic: !row.missing && row.title.length === 0
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            id: weight

            text: row.pages
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: 11
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
                font.pixelSize: 10
                anchors.verticalCenter: parent.verticalCenter
            }

            // Written in full where the other two carry a ring: an empty cell at the end of a
            // line reads as an information that is missing, not as a state.
            Text {
                text: row.neverReadWord
                visible: row.state === 0
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }

            VolumeState {
                visible: row.state === 1 || row.state === 2
                reading: row.state
                howFarRead: row.howFarRead
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // In the reserved gutter, beside the state and never over it. Nothing to command on a
    // file that is not there, so a gap carries no menu even under the pointer.
    CommandMenu {
        id: commands

        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        visible: row.hovered && !row.missing
        seriesId: row.seriesId
        entryId: row.entryId
        fileName: row.fileName
        label: row.title
        onReimportAsked: row.reimportAsked()
    }
}
