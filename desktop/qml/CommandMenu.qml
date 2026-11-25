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
    /// Drawn over an illustration, so it carries its own dark veil. An illustration can be
    /// pale, and grey dots laid straight on one are invisible one time in three.
    property bool veiled: false
    /// The series commanded is one edition among the work's, named on the shelf « Work · Edition ».
    property bool edition: false
    /// Drawn on a row that washes itself at the hover. The button's own hover is the same
    /// colour as that wash, and was drawn on it without a trace: on a row it takes the rule's.
    property bool onAWash: false

    readonly property bool whole: entryId.length === 0
    readonly property bool opened: menu.opened

    /// Asked for a re-import of this series, or of this one file. The host opens the import,
    /// because that dialog belongs to the window and not to a tile.
    signal reimportAsked()
    signal chapterAsked()
    signal universeReimportAsked()
    /// Asked to add editions to this series: files that become editions of it, never its volumes.
    signal editionsAddAsked()
    /// False for an album: it is one book, and has no editions to add to.
    property bool canAddEditions: true

    /// The universe the series belongs to, which a whole-series menu can reimport.
    property string universeId: ""
    property string universeName: ""
    /// Whether the work has editions beside this one, which is the only case where exporting
    /// « the work » is not exporting this.
    property bool otherEditions: edition

    /// How many chapters the volume holds: one can only be replaced where there are some.
    property int chapters: 0

    /// Opens it without the pointer — the Menu key, or Shift+F10, on whatever has the focus.
    /// The desktop's own convention, and it costs nothing to whoever ignores it.
    function show() {
        pointed = false
        menu.open()
    }

    /// Opens it where the secondary button landed, `point` being in `item`'s coordinates.
    /// A menu that answers a click at the other end of a wide tile by appearing under the
    /// dots reads as the wrong menu; the dots and the keys keep the anchor under the button.
    function showAt(item, point) {
        spot = dots.mapFromItem(item, point.x, point.y)
        pointed = true
        menu.open()
    }

    /// Gives the mark to everything picked, and lets go of it: see `SelectionBar.mark`.
    function markSelection(read) {
        Commands.markMany(Selection.ids, Selection.seriesId, read)
        Selection.clear()
    }

    /// Asks where, then exports `scope` — "series", "work" or "universe".
    function exportTo(scope) {
        exportInto.scope = scope
        if (Preferences.lastPlace != "")
            exportInto.currentFolder = Preferences.lastPlace
        exportInto.open()
    }

    property bool pointed: false
    property point spot: Qt.point(0, 0)

    /// Named after what it commands, on the menu *and* on every entry of it: thirty rows
    /// each carry one of these, and « the item called command-erase » would otherwise be
    /// whichever of the thirty the tree happened to hold first.
    readonly property string about: whole ? seriesId : entryId

    /// This one is among several picked: what the menu says applies to all of them. Read and
    /// unread only — whatever opens a window, writes a file or deletes is about one thing,
    /// and is not offered here.
    readonly property bool forSelection: Selection.count > 1 && Selection.ids.includes(about)

    objectName: "commands-" + about
    /// A stop where the keys walk on their own — the header of a series page. On a row or a
    /// tile the row is the stop, and the Menu key on it opens this.
    readonly property bool keyStop: true

    // What it is, said by the button inside it and not again here: a second accessible
    // button around the first, and the window drew no more frames once its menu was open —
    // every run of `crosses-the-seam`, at the click on « Supprimer ».
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter || event.key === Qt.Key_Menu
                || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            dots.show()
            event.accepted = true
        }
    }

    FocusRing {
        objectName: dots.objectName + "-focus"
        cornerRadius: dots.width / 2
    }

    // Round, and the size the design draws it: a button the height of a bar button stamped
    // a square the size of a postage stamp on every cover.
    implicitWidth: side
    implicitHeight: side

    /// The button's size and its glyph's. On a cover, in proportion to the cover — see
    /// `Widths.markOn`. In the header of a series page it is set outright, beside a title in
    /// 30 points; on a row of a list it stays at 28, its glyph at 16.
    property real coverWidth: 0
    property int side: coverWidth > 0 ? Widths.markOn(coverWidth) : 28
    property int glyphSide: Math.round(side * 0.58)
    width: implicitWidth
    height: implicitHeight

    // On an illustration it carries its own dark, which is the design's own value: an
    // illustration can be pale, and grey dots laid straight on one are invisible one time in
    // three. Off an illustration there is nothing to carry.
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        visible: dots.veiled
        // In the colour and not in `opacity`, so the glyph above keeps its own strength.
        color: Qt.rgba(12 / 255, 16 / 255, 14 / 255, 0.74)
    }

    // No tooltip: the label is what this commands, and on a tile it is written under the
    // cover while on a row it is the row itself.
    IconButton {
        id: button

        anchors.fill: parent
        glyph: "more_vert"
        side: dots.glyphSide
        label: dots.label
        color: dots.veiled ? "transparent"
                           : (menu.opened || hovered ? (dots.onAWash ? Theme.rule : Theme.onPaper)
                                                     : "transparent")
        tint: dots.veiled || menu.opened ? Theme.ink : Theme.inkSoft
        popup: menu
        held: menu.opened
    }

    LeafMenu {
        id: menu

        y: button.height + Widths.space.compact


        // Below the button, as every menu opens — or above it when the window has no room
        // below and more above: pushed back inside by its margins, a menu opened from a line
        // near the bottom lay over the button that had opened it.
        onAboutToShow: {
            // Read once and cleared, whoever opens it: the button opens this menu itself and
            // never goes through `show`, so a right click's spot, kept until the next
            // `showAt`, put the menu where the pointer had been when the dots were pressed.
            const underPointer = dots.pointed
            dots.pointed = false
            if (underPointer) {
                // Under the pointer, or above it when the window has no room below — the
                // same rule as for the button, with the pointer as the edge to stay clear of.
                const at = dots.mapToItem(null, dots.spot.x, dots.spot.y)
                const roomUnder = dots.Window.height - at.y - menu.margins
                menu.x = dots.spot.x
                menu.y = menu.implicitHeight > roomUnder && at.y - menu.margins > roomUnder
                        ? dots.spot.y - menu.implicitHeight
                        : dots.spot.y
                return
            }
            menu.x = 0
            const below = dots.mapToItem(null, 0, button.height).y + Widths.space.compact
            const roomBelow = dots.Window.height - below - menu.margins
            const roomAbove = dots.mapToItem(null, 0, 0).y - Widths.space.compact - menu.margins
            menu.y = menu.implicitHeight > roomBelow && roomAbove > roomBelow
                    ? -menu.implicitHeight - Widths.space.compact
                    : button.height + Widths.space.compact
        }

        MenuAction {
            objectName: "command-read-" + dots.about
            glyph: dots.whole ? "done_all" : "check"
            text: dots.forSelection ? Selection.readWord
                                    : (dots.whole ? Commands.words.markSeriesRead
                                                  : Commands.words.markEntryRead)
            onTriggered: dots.forSelection ? dots.markSelection(true)
                                           : (dots.whole ? Commands.markSeries(dots.seriesId, true)
                                                         : Commands.markEntry(dots.entryId, dots.seriesId, true))
        }

        MenuAction {
            objectName: "command-unread-" + dots.about
            glyph: "remove_done"
            text: dots.forSelection ? Selection.unreadWord
                                    : (dots.whole ? Commands.words.markSeriesUnread
                                                  : Commands.words.markEntryUnread)
            onTriggered: dots.forSelection ? dots.markSelection(false)
                                           : (dots.whole ? Commands.markSeries(dots.seriesId, false)
                                                         : Commands.markEntry(dots.entryId, dots.seriesId, false))
        }

        // Correcting what the files say. A profile with the right to edit is offered it, and
        // the window of a volume says that the file is rewritten.
        MenuAction {
            objectName: "command-edit-" + dots.about
            glyph: "edit"
            visible: !dots.forSelection && Profiles.canEdit
            height: visible ? implicitHeight : 0
            text: !dots.whole ? Commands.words.editEntry
                 : dots.edition ? Commands.words.editEdition : Commands.words.editSeries
            onTriggered: {
                editor.active = true
                if (dots.whole)
                    editor.item.show(dots.seriesId)
                else
                    editor.item.show(dots.entryId, dots.seriesId, dots.label)
            }
        }

        // Reading the files again, aimed at this one work and not at the library. A profile that
        // may edit is offered it: it is what picks up a file changed under the server, and what
        // the server itself does after an edit.
        MenuAction {
            objectName: "command-rescan-" + dots.about
            glyph: "refresh"
            visible: !dots.forSelection && Profiles.canEdit && !Scan.running
            height: visible ? implicitHeight : 0
            text: Commands.words.rescan
            onTriggered: dots.whole ? Scan.rescanSeries(dots.seriesId)
                                    : Scan.rescanEntry(dots.entryId)
        }

        // What the file is, and nothing to change: a volume's own, since a series is not one.
        MenuAction {
            objectName: "command-info-" + dots.about
            glyph: "info"
            visible: !dots.forSelection && !dots.whole
            height: visible ? implicitHeight : 0
            text: Commands.words.showInfo
            onTriggered: {
                fileInfo.heading = dots.label
                fileInfo.about = Entries.describe(dots.entryId)
                fileInfo.open()
            }
        }

        // How this series is read, for this profile only: the files' own declaration stays
        // the default, and a choice is taken back with « Par défaut ».
        MenuAction {
            objectName: "command-direction-" + dots.about
            glyph: "menu_book"
            visible: !dots.forSelection && dots.whole
            height: visible ? implicitHeight : 0
            text: Commands.words.chooseDirection
            // Not triggered into the same popup: closing this one and opening the choices
            // beside it is what a second level would be, without a submenu Qt 6.4 draws badly.
            onTriggered: {
                Commands.askDirection(dots.seriesId)
                directions.popup(button, 0, button.height + Widths.space.compact)
            }
        }

        MenuAction {
            objectName: "command-reimport-" + dots.about
            glyph: "library_add"
            // Importing again is importing: a profile without the right is not offered it.
            visible: !dots.forSelection && Profiles.canImport
            height: visible ? implicitHeight : 0
            text: dots.whole ? Commands.words.reimportSeries : Commands.words.reimportEntry
            onTriggered: dots.reimportAsked()
        }

        MenuAction {
            objectName: "command-editions-" + dots.about
            glyph: "library_add"
            visible: !dots.forSelection && dots.whole && dots.canAddEditions && Profiles.canImport
            height: visible ? implicitHeight : 0
            text: Commands.words.addEditions
            onTriggered: dots.editionsAddAsked()
        }

        MenuAction {
            objectName: "command-universe-" + dots.about
            glyph: "library_add"
            visible: !dots.forSelection && dots.whole && dots.universeId.length > 0
                     && Profiles.canImport
            height: visible ? implicitHeight : 0
            text: ImportCaptions.reimportUniverseLabel(dots.universeName)
            onTriggered: dots.universeReimportAsked()
        }

        MenuAction {
            objectName: "command-chapter-" + dots.about
            glyph: "swap_horiz"
            visible: !dots.forSelection && !dots.whole && dots.chapters > 0 && Profiles.canImport
            height: visible ? implicitHeight : 0
            text: Commands.words.replaceChapter
            onTriggered: dots.chapterAsked()
        }

        // A series is not a file, so there is nothing to save a copy of.
        MenuAction {
            objectName: "command-copy-" + dots.about
            glyph: "download"
            // And a profile that may not take a volume out of the library is not offered one.
            visible: !dots.forSelection && !dots.whole && Profiles.canExport
            height: visible ? implicitHeight : 0
            text: Commands.words.saveACopy
            onTriggered: {
                // The server's name for a copy, and the file's own where it gives none.
                where.proposed = Entries.describe(dots.entryId).copyName || dots.fileName
                where.open()
            }
        }

        // What leaves the library as the folders Leaf reads back. The folder is asked every
        // time: an export is a copy for somewhere, and the last place is rarely the next.
        MenuAction {
            objectName: "command-export-" + dots.about
            glyph: "upload"
            // Not on a volume: « Enregistrer une copie… » already hands over its file, and an
            // export of one volume is the same file under the folders around it.
            visible: !dots.forSelection && dots.whole && Profiles.canExport
            height: visible ? implicitHeight : 0
            enabled: !Exports.busy
            text: Commands.words.export
            onTriggered: dots.exportTo("series")
        }

        MenuAction {
            objectName: "command-export-work-" + dots.about
            glyph: "upload"
            visible: !dots.forSelection && dots.whole && dots.otherEditions && Profiles.canExport
            height: visible ? implicitHeight : 0
            enabled: !Exports.busy
            text: Commands.words.exportWork
            onTriggered: dots.exportTo("work")
        }

        MenuAction {
            objectName: "command-export-universe-" + dots.about
            glyph: "upload"
            visible: !dots.forSelection && dots.whole && dots.universeName.length > 0
                     && Profiles.canExport
            height: visible ? implicitHeight : 0
            enabled: !Exports.busy
            text: Commands.words.exportUniverse
            onTriggered: dots.exportTo("universe")
        }

        MenuAction {
            objectName: "command-erase-" + dots.about
            glyph: "delete"
            dangerous: true
            // Erasing is a right of its own: the server refuses it without, and a menu that
            // offers what is refused is a menu that lies.
            visible: !dots.forSelection && Profiles.canErase
            height: visible ? implicitHeight : 0
            text: !dots.whole ? Commands.words.eraseEntry
                 : dots.edition ? Commands.words.eraseEdition : Commands.words.eraseSeries
            onTriggered: dots.whole ? Erasure.aboutSeries(dots.seriesId)
                                    : Erasure.aboutEntry(dots.entryId, dots.seriesId)
        }
    }

    FileInfoDialog {
        id: fileInfo
    }

    // Built the first time it is asked for: a hundred tiles are a hundred menus, and a window
    // with a dozen fields is not something a tile that nobody edits should carry.
    Loader {
        id: editor

        active: false
        sourceComponent: dots.whole ? seriesEditor : entryEditor
    }

    Component { id: seriesEditor; SeriesEditDialog {} }
    Component { id: entryEditor; EntryEditDialog {} }

    LeafMenu {
        id: directions

        objectName: "direction-menu-" + dots.seriesId

        Repeater {
            model: ReaderCaptions.directions

            delegate: LeafMenuItem {
                required property var modelData

                objectName: "direction-" + modelData.value + "-" + dots.seriesId
                text: modelData.label
                checkable: true
                checked: Commands.direction === modelData.value
                onTriggered: Commands.chooseDirection(dots.seriesId, modelData.value)
            }
        }
    }

    FolderDialog {
        id: exportInto

        property string scope: "series"

        objectName: "export-folder"
        title: Exports.words.folderTitle
        onAccepted: {
            Preferences.rememberPlace(selectedFolder)
            if (scope === "work")
                Exports.exportWork(dots.seriesId, selectedFolder)
            else if (scope === "universe")
                Exports.exportUniverse(dots.seriesId, selectedFolder)
            else
                Exports.exportSeries([dots.seriesId], selectedFolder, dots.label)
        }
    }

    FileDialog {
        id: where

        property string proposed: ""

        objectName: "save-a-copy"
        fileMode: FileDialog.SaveFile
        currentFile: where.proposed.length > 0 ? "file:" + where.proposed : ""
        onAccepted: Commands.saveACopy(dots.entryId, selectedFile)
    }
}
