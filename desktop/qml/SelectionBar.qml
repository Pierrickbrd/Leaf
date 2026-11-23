// What is picked, and the commands that apply to all of it.
//
// A bar over the foot of the page and not a mode of the page: picking a few covers must leave
// the shelf as it was, and the bar goes with the last one put back. Laid over the screen and
// taking no room from it, like the bubbles.
//
// Only what is the same for every member and cannot be regretted: read and unread. Erasing is
// deliberately not here — a bar one click from deleting twenty files is a bar that will, one
// day, and the menu of a picked item offers no erasure either.
//
// The keys reach the same two commands from the menu of any picked item (the Menu key), and
// Escape empties the selection; these buttons are for the pointer, and are stops all the same.

import QtQuick
import QtQuick.Dialogs
import Leaf

Item {
    id: layer

    objectName: "selection-bar"
    anchors.fill: parent
    // A layer with nothing in it takes no clicks: the page underneath is still a page.
    visible: Selection.active
    z: 40

    Rectangle {
        id: bar

        objectName: "selection-card"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Widths.space.gutter
        width: Math.min(layer.width - 2 * Widths.space.gutter, row.implicitWidth + 2 * Widths.space.card)
        height: row.implicitHeight + 2 * Widths.space.compact
        radius: Theme.cardRadius
        color: Theme.surface
        border.color: Theme.rule
        border.width: 1
        antialiasing: true

        CardLift { level: CardLift.Bubble }

        Accessible.role: Accessible.ToolBar
        Accessible.name: Selection.label

        Row {
            id: row

            anchors.centerIn: parent
            spacing: Widths.space.compact

            Text {
                objectName: "selection-count"
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: Selection.label
                color: Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(13)
                font.weight: Font.Medium
                rightPadding: Widths.space.compact
            }

            LeafTextAction {
                objectName: "selection-read"
                label: Selection.readWord
                onTriggered: layer.mark(true)
            }

            LeafTextAction {
                objectName: "selection-unread"
                label: Selection.unreadWord
                onTriggered: layer.mark(false)
            }

            // Series only: a selection of volumes is not a thing that has folders above it.
            LeafTextAction {
                objectName: "selection-export"
                visible: Selection.seriesId.length === 0 && Profiles.canExport
                enabled: !Exports.busy
                label: Selection.exportWord
                onTriggered: exportInto.open()
            }

            LeafTextAction {
                objectName: "selection-clear"
                label: Selection.clearWord
                quiet: true
                onTriggered: Selection.clear()
            }
        }
    }

    /// Marks what is picked and lets go of it: the tiles are drawn again from the answers,
    /// and a selection kept would mark the same twenty again at the next press.
    function mark(read) {
        Commands.markMany(Selection.ids, Selection.seriesId, read)
        Selection.clear()
    }

    FolderDialog {
        id: exportInto

        objectName: "selection-export-folder"
        title: Exports.words.folderTitle
        onAccepted: {
            Preferences.rememberPlace(selectedFolder)
            Exports.exportSeries(Selection.ids, selectedFolder, "")
            Selection.clear()
        }
    }
}
