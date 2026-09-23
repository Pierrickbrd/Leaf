// The three dots, and what they open.
//
// The same control in four places — a shelf tile, the header of a series, a line of its list,
// a tile of its grid — because an object drawn somewhere must be commandable where it is
// drawn. What changes between them is where it sits and when it appears, never what it is.
//
// On a cover it appears at the hover and at the focus, and nowhere is it permanent: fifty
// « … » over fifty illustrations are fifty stains on a wall the application exists to show.

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Leaf

Item {
    id: dots

    /// What this commands. An empty `entryId` is the menu of a whole edition.
    required property string seriesId
    property string entryId: ""
    /// What it is called, for the tooltip and for whoever reads the screen aloud.
    required property string label
    /// The file this volume is, which is the name a copy is offered under.
    property string fileName: ""

    readonly property bool whole: entryId.length === 0
    readonly property bool opened: menu.opened

    /// Asked for a re-import of this series, or of this one file. The host opens the import,
    /// because that dialog belongs to the window and not to a tile.
    signal reimportAsked()

    /// Opens it without the pointer — the Menu key, or Shift+F10, on whatever has the focus.
    /// The desktop's own convention, and it costs nothing to whoever ignores it.
    function show() {
        menu.open()
    }

    /// Named after what it commands, on the menu *and* on every entry of it: thirty rows
    /// each carry one of these, and « the item called command-erase » would otherwise be
    /// whichever of the thirty the tree happened to hold first.
    readonly property string about: whole ? seriesId : entryId

    objectName: "commands-" + about
    implicitWidth: 30
    implicitHeight: 30

    BarButton {
        id: button

        anchors.fill: parent
        source: "assets/icons/more_vert.svg"
        label: dots.label
        popup: menu
        held: menu.opened
    }

    LeafMenu {
        id: menu

        y: button.height + 2

        MenuAction {
            objectName: "command-read-" + dots.about
            glyph: dots.whole ? "done_all" : "check"
            text: dots.whole ? Commands.words.markSeriesRead : Commands.words.markEntryRead
            onTriggered: dots.whole ? Commands.markSeries(dots.seriesId, true)
                                    : Commands.markEntry(dots.entryId, dots.seriesId, true)
        }

        MenuAction {
            objectName: "command-unread-" + dots.about
            glyph: "remove_done"
            text: dots.whole ? Commands.words.markSeriesUnread
                             : Commands.words.markEntryUnread
            onTriggered: dots.whole ? Commands.markSeries(dots.seriesId, false)
                                    : Commands.markEntry(dots.entryId, dots.seriesId, false)
        }

        MenuAction {
            objectName: "command-reimport-" + dots.about
            glyph: "library_add"
            text: dots.whole ? Commands.words.reimportSeries : Commands.words.reimportEntry
            onTriggered: dots.reimportAsked()
        }

        // A series is not a file, so there is nothing to save a copy of.
        MenuAction {
            objectName: "command-copy-" + dots.about
            glyph: "download"
            visible: !dots.whole
            height: visible ? implicitHeight : 0
            text: Commands.words.saveACopy
            onTriggered: where.open()
        }

        MenuAction {
            objectName: "command-erase-" + dots.about
            glyph: "delete"
            dangerous: true
            text: dots.whole ? Commands.words.eraseSeries : Commands.words.eraseEntry
            onTriggered: dots.whole ? Erasure.aboutSeries(dots.seriesId)
                                    : Erasure.aboutEntry(dots.entryId, dots.seriesId)
        }
    }

    FileDialog {
        id: where

        objectName: "save-a-copy"
        fileMode: FileDialog.SaveFile
        currentFile: dots.fileName.length > 0 ? "file:" + dots.fileName : ""
        onAccepted: Commands.saveACopy(dots.entryId, selectedFile)
    }
}
