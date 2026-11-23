// Where files are dropped, questions answered, and a transfer watched.
//
// **Closing stops nothing.** That is the one thing this file exists to get right. The queue
// lives in `Imports` and survives being closed; this is a window onto it. Without that,
// "the dialog blocks the rest" and "go back to reading, it carries on" cannot both be true,
// and they were both asked for.
//
// Which is why the × is two commands wearing one glyph. It closes, and it throws away what
// was being *prepared* — never what is already moving. Opening the dialog during a transfer,
// stepping back through the phases and closing does not touch that transfer.

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Leaf

Popup {
    id: dialog

    /// Which of the four the dialog is showing. Not derived from the queue: a reader who
    /// stepped back to phase one must stay there while a transfer runs behind them.
    property int phase: choosing

    readonly property int choosing: 0
    readonly property int proposing: 1
    readonly property int watching: 2
    /// The checking frame, over the cards: the second of the three steps the header numbers.
    readonly property int verifying: 3

    /// The step the header lights: the cards and the drop zone are both « Choisir ».
    readonly property int step: phase === verifying ? 2 : phase === watching ? 3 : 1

    /// The third step, and nothing of it has gone yet: the foot says what « Importer tout »
    /// will do, and once a line has gone, that closing the window stops nothing. A line put
    /// back to zero is not one that went. « Précédent » leads back to the checking step either
    /// way: a button that changed its word and its destination once a line was sent sent the
    /// reader to the drop zone with the list emptied, to look for the series they had just
    /// launched.
    readonly property bool unsent: phase === watching && !ImportTally.started

    /// What a reimport is aimed at, set by `showFor`; empty for an import that aims at
    /// nothing. The drop zone, the heading and the footer read it, and a plain `show` clears
    /// it.
    property string aimKind: ""
    property string aimId: ""
    property string aimName: ""
    /// How many volumes the library holds of it, 0 when the menu did not know.
    property int aimVolumes: 0

    /// The two-column drawing of « Choisir »: the drop zone stays on the left while what it
    /// read stands on the right. Without the cards the zone has the whole width.
    readonly property bool split: phase === proposing

    /// The height of the body — what stands between the heading and the footer — under which
    /// the box that asks for the checking drops its second paragraph and keeps its title. It
    /// is read in the window's own units: at the smallest window the app allows (800 × 560)
    /// the body is some 380 high, and the box, the line under it and the drop zone's own
    /// words are what that has to hold.
    readonly property int verifyAsideNeeds: 520
    readonly property bool cramped: commands.y - heading.height < Widths.scale.column(dialog.verifyAsideNeeds)

    /// Whether anything is still being set up — what the × would throw away.
    readonly property bool preparing: phase !== watching && Imports.count > 0

    objectName: "import-dialog"
    // As wide and as tall as its words, and never closer to the window's edge than the
    // page's gutter: held 80 pixels off it whatever the window, it sat at 620 × 480 in a
    // window of 800 × 560 with a well of 260 pixels of nothing (audit, 23/10/2026), and a
    // text size a quarter larger had nowhere to go. The well takes what is left.
    //
    // The frame takes more: three columns of words do not stand in 620, and it is drawn at
    // 1120 × 760 for a window of 1280. Every phase is drawn at that size: the window does not
    // change size under the hand that is going to the next step, and the title of a
    // reimport — « Réimporter Dragon Ball – Édition Perfect » — has the room of a whole
    // line, where 620 left it 415 pixels and cut it after « Dragon Ba ».
    readonly property int drawnWidth: 1120
    readonly property int drawnHeight: 760
    width: Math.max(0, Math.min(Widths.scale.column(drawnWidth),
                                Overlay.overlay ? Overlay.overlay.width - 2 * Widths.space.gutter
                                                : Widths.scale.column(drawnWidth)))
    height: Math.max(0, Math.min(Widths.scale.column(drawnHeight),
                                 Overlay.overlay ? Overlay.overlay.height - 2 * Widths.space.gutter
                                                 : Widths.scale.column(drawnHeight)))
    anchors.centerIn: Overlay.overlay
    padding: 0
    modal: true
    focus: true
    // Escape is the × — it means the same thing and a reader reaches for it first.
    closePolicy: Popup.CloseOnEscape

    // What arrived leaves the queue with the window, however it was shut: it reopens on what is
    // still to do, and on the drop zone when nothing is. A reader who launched two of four and
    // shut it for a while finds the other two where they were.
    onClosed: {
        ImportQueue.clearFiled()
        if (Imports.count === 0)
            phase = choosing
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, Theme.dark ? 0.62 : 0.38)
    }

    background: Rectangle {
        objectName: "import-dialog-surface"
        radius: Theme.cardRadius
        color: Theme.paper
        border.color: Theme.rule
        border.width: 1
        antialiasing: true
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 110 }
    }

    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80 }
    }

    // Abandoning the last card left the dialog on an empty step: the list had nothing left
    // to show and the drop zone was two screens back. A queue that empties while still being
    // prepared goes back to where one starts again.
    //
    // And a card that comes back to a tick while the transfers are watched — one corrected
    // and announced again after « Importer » — is a question the cards ask: left on the
    // transfers, it waited there for an answer nothing on screen asked for.
    //
    // Empty once the change is over, not in its middle: a lone card turned shelf is abandoned
    // before the two it becomes are added, and the queue empty for that instant sent the
    // frame back to the drop zone, the keys with it.
    Connections {
        target: Imports

        function onChanged() {
            if ((dialog.phase === dialog.proposing || dialog.phase === dialog.verifying)
                    && Imports.count === 0)
                Qt.callLater(dialog.emptied)
            else if (dialog.phase === dialog.watching && Imports.deciding > 0)
                dialog.goTo(dialog.proposing)
        }
    }

    function emptied() {
        if ((dialog.phase === dialog.proposing || dialog.phase === dialog.verifying)
                && Imports.count === 0)
            dialog.goTo(dialog.choosing)
    }

    /// Moves to the step `to` with the keys: taken off whatever held them first — a button
    /// that disappears with its step cannot keep them, and Qt says so out loud — and given,
    /// once the step is drawn, to where that step opens (`initialFocus`).
    function goTo(to) {
        body.forceActiveFocus(Qt.OtherFocusReason)
        phase = to
        Qt.callLater(body.settle)
    }

    contentItem: DialogBody {
        id: body

        // The first way in while nothing is chosen; with files ready, the button that sends
        // them. The pickers that held the keys go when something is taken in, and the keys
        // were left on nothing. Until the files are ready that button takes no Enter, so
        // waiting on it loses nothing either.
        initialFocus: dialog.phase === dialog.choosing ? chooseFiles
                    : dialog.phase === dialog.proposing ? startAction
                    : dialog.phase === dialog.verifying ? frame.firstStop : backAction

        // ——— The heading, and the way out ———————————————————————————————————

        Item {
            id: heading

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            // From its words, a card's padding above and under them: a literal 56 held a
            // title that a larger desktop text made taller than it.
            height: Math.max(Widths.scale.size(56), named.height + 2 * Widths.space.card)

            Column {
                id: named

                anchors.left: parent.left
                anchors.leftMargin: Widths.space.card
                anchors.right: steps.left
                anchors.rightMargin: Widths.space.row
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    id: title

                    objectName: "import-title"
                    width: parent.width
                    textFormat: Text.PlainText
                    text: dialog.aimKind.length > 0
                          ? ImportCaptions.reimportTitle(dialog.aimKind, dialog.aimName)
                          : ImportCaptions.title
                    color: Theme.ink
                    font.family: Theme.displayFamily
                    // As the three steps were drawn: the window's one heading, over a frame of
                    // three columns and over the drop zone alike.
                    font.pixelSize: Widths.scale.title(32)
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }

                // What was dropped, in one line, under the title of the frame that shows it —
                // and, while the files are awaited, what the library holds of what they aim at.
                Text {
                    objectName: "import-subtitle"
                    width: parent.width
                    visible: text.length > 0
                    text: dialog.phase === dialog.verifying
                          ? Review.subtitle
                          : dialog.phase === dialog.choosing
                            ? ImportCaptions.reimportHolds(dialog.aimKind, dialog.aimVolumes)
                            : ""
                    textFormat: Text.PlainText
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                    elide: Text.ElideRight
                }
            }

            // Against the cross, and not centred: centred, they cut the title in half the
            // window's width, and the name of an edition is a long title.
            ImportSteps {
                id: steps

                objectName: "import-steps"
                current: dialog.step
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: close.left
                anchors.rightMargin: Widths.space.card
            }

            // The word and not the cross, as the drawing has it: a cross reads as « cancel »,
            // and this one only hides the window.
            QuietButton {
                id: close

                objectName: "import-close"
                scaled: true
                anchors.right: parent.right
                anchors.rightMargin: Widths.scale.size(12)
                anchors.verticalCenter: parent.verticalCenter
                label: ImportCaptions.closeLabel
                // It hides the window. It does not decide anything.
                //
                // It used to reset the phase and give up whatever was being prepared, so
                // closing during a verification threw away the reading of twenty-seven
                // gigabytes, and closing on a transfer left it running with no way back to
                // it — four gigabytes sat in the server's inbox because nobody could reach
                // the card again. Giving up is what « Revenir en arrière » and
                // « Abandonner » are for, and both say so.
                onTriggered: dialog.close()
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.rule
            }
        }

        // ——— Phase one: nothing chosen yet ——————————————————————————————————

        DropArea {
            id: well

            objectName: "import-well"
            anchors.left: parent.left
            anchors.top: heading.bottom
            anchors.margins: Widths.space.card
            // From what it holds and not from the window: a drop zone that took every pixel
            // left over was a box of emptiness with three lines lost in it. The most it takes is
            // what is left above the box and the line that stand at the foot of the column.
            readonly property real room: leftBottom.y - heading.height - 2 * Widths.space.card
            height: Math.max(0, Math.min(well.room, Widths.scale.fit(460),
                                         Math.max(invitation.implicitHeight
                                                  + 2 * Widths.scale.size(56), well.room * 0.62)))
            width: dialog.split ? Math.round((parent.width - 3 * Widths.space.card) * 0.52)
                                : parent.width - 2 * Widths.space.card
            visible: dialog.phase === dialog.choosing || dialog.split
            onDropped: drop => dialog.take(drop.urls)

            // Dashed, and drawn rather than set: a `Rectangle` border is solid, and a solid
            // outline around an empty box reads as a panel — something the window is made
            // of. The dashes are what say « put something here ».
            //
            // `Canvas` and not `QtQuick.Shapes`, which is a module this build does not
            // package — and the client already draws its arrow this way.
            Canvas {
                id: edge

                anchors.fill: parent
                onPaint: {
                    const ink = getContext("2d")
                    ink.reset()
                    // Emerald and washed always, as the drawing has it: grey dashes read as
                    // a place nothing is expected, and this is the one the window asks for.
                    ink.fillStyle = Theme.emeraldWash
                    ink.beginPath()
                    ink.roundedRect(0, 0, width, height, Theme.cardRadius, Theme.cardRadius)
                    ink.fill()
                    ink.strokeStyle = Theme.emerald
                    ink.lineWidth = well.containsDrag ? 2 : 1
                    ink.setLineDash([7, 6])
                    ink.beginPath()
                    // Inset by half the stroke, or the outer half of it falls outside the
                    // item and is clipped to a thinner line on two sides than on the others.
                    const edgeAt = ink.lineWidth / 2
                    ink.roundedRect(edgeAt, edgeAt, width - ink.lineWidth,
                                    height - ink.lineWidth, Theme.cardRadius,
                                    Theme.cardRadius)
                    ink.stroke()
                }

                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                Connections {
                    target: well

                    function onContainsDragChanged() {
                        edge.requestPaint()
                    }
                }

                Connections {
                    target: Theme

                    function onChanged() {
                        edge.requestPaint()
                    }
                }
            }

            Column {
                id: invitation

                anchors.centerIn: parent
                spacing: Widths.space.row

                // A folder with an arrow into it, drawn: the client embeds no icon set.
                Canvas {
                    id: folderIcon

                    objectName: "import-well-icon"
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Widths.scale.size(44)
                    height: width
                    onPaint: {
                        const ink = getContext("2d")
                        ink.reset()
                        ink.scale(width / 24, height / 24)
                        ink.strokeStyle = Theme.emerald
                        ink.lineWidth = 1.6
                        ink.lineCap = "round"
                        ink.lineJoin = "round"
                        ink.beginPath()
                        ink.moveTo(3, 7)
                        ink.arcTo(3, 5, 5, 5, 2)
                        ink.lineTo(9, 5)
                        ink.lineTo(11, 7)
                        ink.lineTo(19, 7)
                        ink.arcTo(21, 7, 21, 9, 2)
                        ink.lineTo(21, 17)
                        ink.arcTo(21, 19, 19, 19, 2)
                        ink.lineTo(5, 19)
                        ink.arcTo(3, 19, 3, 17, 2)
                        ink.closePath()
                        ink.moveTo(12, 11)
                        ink.lineTo(12, 16)
                        ink.moveTo(9.5, 13.5)
                        ink.lineTo(12, 11)
                        ink.lineTo(14.5, 13.5)
                        ink.stroke()
                    }
                    Connections {
                        target: Theme

                        function onChanged() {
                            folderIcon.requestPaint()
                        }
                    }
                }

                Text {
                    objectName: "import-well-words"
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(implicitWidth, well.width - 2 * Widths.space.card)
                    textFormat: Text.PlainText
                    text: dialog.aimKind.length > 0 ? DropCaptions.aimedDropLabel(dialog.aimKind)
                                                    : DropCaptions.dropHereLabel
                    color: Theme.emerald
                    font.family: Theme.displayFamily
                    font.pixelSize: Widths.scale.title(26)
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                Text {
                    objectName: "import-well-what"
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(Widths.scale.column(340), well.width - 2 * Widths.space.card)
                    visible: dialog.aimKind.length === 0
                    textFormat: Text.PlainText
                    text: DropCaptions.dropWhatLabel
                    color: Theme.emerald
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(13)
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                // Two pickers and not one, because the desktop has two: `FileDialog` cannot
                // return a folder and `FolderDialog` cannot return a file, and a single
                // button would do half the job without saying which half. Two buttons of one
                // size — the folder first, in the emerald, being what a library is made of.
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    topPadding: Widths.space.row
                    spacing: Widths.space.row

                    ActionButton {
                        id: chooseFolder

                        objectName: "import-choose-folder"
                        scaled: true
                        height: Widths.scale.fit(40)
                        activeFocusOnTab: true
                        label: DropCaptions.chooseFolderLabel
                        onTriggered: {
                            if (Preferences.lastPlace != "")
                                folder.currentFolder = Preferences.lastPlace
                            folder.open()
                        }
                    }

                    QuietButton {
                        id: chooseFiles

                        objectName: "import-choose-files"
                        scaled: true
                        height: Widths.scale.fit(40)
                        // The window opens on the first way in when nothing is chosen yet.
                        focus: dialog.phase === dialog.choosing
                        label: DropCaptions.chooseFilesLabel
                        onTriggered: {
                            if (Preferences.lastPlace != "")
                                files.currentFolder = Preferences.lastPlace
                            files.open()
                        }
                    }
                }
            }
        }

        // What this step does and does not do, under the box: nothing leaves the machine here.
        Text {
            id: reassurance

            objectName: "import-reassurance"
            anchors.left: well.left
            anchors.right: well.right
            anchors.top: well.bottom
            anchors.topMargin: Widths.space.row
            text: DropCaptions.reassuranceLabel
            textFormat: Text.PlainText
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
            lineHeight: 1.3
            wrapMode: Text.WordWrap
            // Where it fits between the zone and what stands at the foot of the column, and
            // nowhere else: the footer says the same thing in fewer words, so a window that
            // cannot spare the room loses nothing by it.
            visible: (dialog.phase === dialog.choosing || dialog.split)
                     && well.y + well.height + Widths.space.row + implicitHeight
                        <= leftBottom.y - Widths.space.card
        }

        // Under the dashed box and not inside it. The dashes mean « put something here »,
        // and a setting enclosed by them reads as part of the target rather than as how
        // what lands in it will be read. Dropping onto it still works: the window itself
        // is a drop area — see `Main.qml`.
        //
        // Read when a folder is taken in, never after: the fingerprints are computed while
        // the manifest is built, and a session announced without them cannot grow them
        // afterwards. So it can be changed only before anything is chosen; once the cards are
        // shown it stays, greyed, to say what they were read with.
        //
        // Both at the foot of the left column, level with the note that stands at the foot of
        // the right one, so that what grows between the zone and them is empty paper and not
        // a box that moved.
        Column {
            id: leftBottom

            objectName: "import-left-bottom"
            anchors.left: well.left
            anchors.right: well.right
            anchors.bottom: commands.top
            anchors.bottomMargin: Widths.space.card
            spacing: Widths.space.card
            visible: dialog.phase === dialog.choosing || dialog.split

            LeafCheck {
                id: verify

                objectName: "import-verify"
                width: parent.width
                activeFocusOnTab: enabled
                enabled: Imports.count === 0
                opacity: enabled ? 1.0 : 0.6
                label: DropCaptions.verifyLabel
                aside: dialog.cramped ? "" : DropCaptions.verifyAside
                checked: Imports.verifying
                onToggled: wanted => Imports.verifying = wanted
            }

            ImportStatus {
                id: status

                width: parent.width
                visible: dialog.split
                counting: Imports.foldersToRead > 0 || ImportTally.reading > 0
                words: DropCaptions.dropProgressLabel({
                    foldersRead: Imports.foldersRead,
                    foldersToRead: Imports.foldersToRead,
                    reading: ImportTally.reading,
                    checked: ImportTally.volumesChecked,
                    toCheck: ImportTally.volumesToCheck,
                    hashing: Imports.verifying,
                    deciding: Imports.deciding,
                    listed: ImportTally.listed
                })
                // The volumes once a folder has said how many it holds, the folders before.
                fraction: ImportTally.volumesToCheck > 0
                          ? ImportTally.volumesChecked / ImportTally.volumesToCheck
                          : Imports.foldersRead / Math.max(1, Imports.foldersRead
                                                              + Imports.foldersToRead)
            }
        }

        // Both pickers open where they were last used. A library lives in one place and a
        // reader imports from it over and over; opening on the home folder every time meant
        // walking the same four levels down before every single import. `currentFolder` is
        // left alone when nothing has been remembered yet, so the first use keeps whatever
        // the desktop hands over.
        FileDialog {
            id: files

            objectName: "import-file-picker"
            title: DropCaptions.chooseFilesTitle
            fileMode: FileDialog.OpenFiles
            onAccepted: {
                // The folder the chosen file sits in: what a reader wants next time is the
                // place, not the thing they took out of it.
                if (selectedFiles.length > 0)
                    Preferences.rememberPlace(currentFolder)
                dialog.take(selectedFiles)
            }
        }

        FolderDialog {
            id: folder

            objectName: "import-folder-picker"
            title: DropCaptions.chooseFolderTitle
            onAccepted: {
                // The folder it sits in, not the folder itself: a reader who imported
                // « Bleach » is far more likely to want « Dragon Ball » next than to want
                // to look inside « Bleach ».
                Preferences.rememberPlace(currentFolder)
                dialog.take([selectedFolder])
            }
        }

        // ——— Phases two and three: one row per file —————————————————————————

        // ——— The right column of « Choisir »: what was read ——————————————————
        //
        // A title and what it weighs, then one line per thing read — compact, a name and what
        // it is, nothing to unfold: the checking step is where it opens — then what the library
        // already holds, at the foot of the column.

        Item {
            id: shelf

            objectName: "import-shelf"
            anchors.left: well.right
            anchors.leftMargin: Widths.space.card
            anchors.right: parent.right
            anchors.rightMargin: Widths.space.card
            anchors.top: well.top
            anchors.bottom: commands.top
            anchors.bottomMargin: Widths.space.card
            visible: dialog.split

            Text {
                id: shelfTitle

                objectName: "import-shelf-title"
                anchors.left: parent.left
                anchors.top: parent.top
                text: DropCaptions.readSoFar(Imports.count)
                textFormat: Text.PlainText
                color: Theme.ink
                font.family: Theme.displayFamily
                font.pixelSize: Widths.scale.title(24)
                font.weight: Font.DemiBold
            }

            // What the drop weighs: how much would travel, so a reader sees the cost before
            // looking at a line. Said once the walks are over: while they run it is 0 files
            // and 0 bytes, which is not a fact but an absence of one.
            Text {
                objectName: "import-total"
                anchors.right: parent.right
                anchors.baseline: shelfTitle.baseline
                visible: Imports.foldersToRead === 0
                text: DropCaptions.toSendTotal(ImportTally.toSendFiles, ImportTally.toSendBytes)
                textFormat: Text.PlainText
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
            }

            // What stands under the list at the foot of the column: the note. The list takes
            // what it leaves.
            readonly property real heldReserve: held.visible ? held.height + Widths.space.row : 0

            // The list, in one box with a rule between its lines. It is as tall as its lines
            // and no taller, and scrolls inside itself when forty folders are more than the
            // window has — the note at the foot of the column does not move at all.
            Rectangle {
                id: listBox

                objectName: "import-lines-box"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: shelfTitle.bottom
                anchors.topMargin: Widths.space.row
                height: Math.max(0, Math.min(lines.contentHeight + 2,
                                             shelf.height - shelfTitle.height - 2 * Widths.space.row
                                             - Widths.space.row
                                             - shelf.heldReserve))
                radius: Theme.cardRadius
                color: Theme.surface
                border.color: Theme.rule
                border.width: 1
                antialiasing: true

                // Its lines paint nothing of their own behind the box's rounding, so the list
                // is clipped square inside it and the corners stay the box's.
                ListView {
                    id: lines

                    objectName: "import-lines"
                    anchors.fill: parent
                    anchors.margins: 1
                    model: Imports
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    interactive: contentHeight > height

                    delegate: Item {
                        id: entry

                        required property int index
                        required property string name
                        required property int stage
                        required property var size
                        required property string reason
                        required property var candidates
                        required property string trouble
                        required property bool folder
                        required property var moves
                        required property var nodes
                        required property string checking
                        required property int readHow
                        required property var progress

                        width: lines.width
                        height: item.height + (entry.index > 0 ? 1 : 0)

                        // The rule between two lines, and none above the first.
                        Rectangle {
                            visible: entry.index > 0
                            width: parent.width
                            height: 1
                            color: Theme.rule
                        }

                        ImportLine {
                            id: item

                            y: entry.index > 0 ? 1 : 0
                            width: parent.width
                            row: entry.index
                            name: entry.name
                            stage: entry.stage
                            size: entry.size
                            reason: entry.reason
                            candidates: entry.candidates
                            trouble: entry.trouble
                            folder: entry.folder
                            moves: entry.moves
                            nodes: entry.nodes
                            checking: entry.checking
                            readHow: entry.readHow
                            progress: entry.progress
                            onAnswered: seriesId => Imports.decide(entry.index, seriesId)
                            onAgreed: Imports.accept(entry.index)
                            onWantsFiling: (workId, wanted) =>
                                Imports.setFiling(entry.index, workId, wanted)
                            onDropped: Imports.abandon(entry.index)
                        }
                    }
                }
            }

            // What the library already holds of the drop, in a box of its own — and at the foot
            // of the column, where it stays when forty folders make the list scroll above it.
            Rectangle {
                id: held

                objectName: "import-held"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: heldWords.implicitHeight + 2 * Widths.space.row
                radius: Theme.cardRadius
                color: Theme.surface
                visible: ImportTally.heldFiles > 0

                Text {
                    id: heldWords

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Widths.space.row
                    // By name when a card says which series it is, as the drawing has it;
                    // the count of files stays for a drop that names none.
                    text: ImportTally.heldNote.length > 0
                          ? ImportTally.heldNote : DropCaptions.heldNotResent(ImportTally.heldFiles)
                    textFormat: Text.PlainText
                    color: Theme.ink
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(13)
                    wrapMode: Text.Wrap
                }
            }
        }

        // The third step, before anything goes and while it does: one list of what is sent, each
        // line with the button that sends it. The queue's own cards, which this replaced, were
        // for what moves; they drew the same things a second time, with a tree and ticks.
        ImportRecap {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: heading.bottom
            anchors.bottom: commands.top
            anchors.margins: Widths.space.card
            visible: dialog.phase === dialog.watching
        }

        // ——— Step two: the checking frame ————————————————————————————————————

        ImportReview {
            id: frame

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: heading.bottom
            anchors.bottom: parent.bottom
            visible: dialog.phase === dialog.verifying
            onBack: dialog.goTo(dialog.proposing)
            // A series that would move now waits for its tick, and one held back by what it
            // says waits to be corrected, both on the cards: going on to the transfers watched
            // one that never began, and left the other with nothing offering to send it.
            onImported: dialog.goTo(Imports.deciding > 0 || ImportTally.blocked > 0
                                    ? dialog.proposing : dialog.watching)
            onDone: {
                dialog.goTo(dialog.choosing)
                dialog.close()
            }
        }

        // ——— What the queue is doing as a whole ——————————————————————————————

        Item {
            id: commands

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: dialog.phase !== dialog.verifying
            // A card's padding round the tallest of its buttons and its words, not a literal
            // 64 a sentence of trouble two lines tall ran out of.
            height: Math.max(Widths.scale.size(64),
                             Math.max(buttons.height, aside.height)
                             + 2 * Widths.space.card)

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.rule
            }

            Column {
                id: aside

                anchors.left: parent.left
                anchors.leftMargin: Widths.space.card
                anchors.right: buttons.left
                anchors.rightMargin: Widths.space.row
                anchors.verticalCenter: parent.verticalCenter
                spacing: Widths.space.tight

                // The footer's one sentence while the cards stand beside the zone.
                Text {
                    objectName: "import-checked"
                    width: parent.width
                    visible: dialog.split && Imports.trouble.length === 0
                    text: DropCaptions.everythingIsCheckedLabel
                    textFormat: Text.PlainText
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                    wrapMode: Text.Wrap
                }

                Text {
                    id: trouble

                    objectName: "import-trouble"
                    width: parent.width
                    visible: text.length > 0
                    textFormat: Text.PlainText
                    text: Imports.trouble
                    color: Theme.alert
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(13)
                    wrapMode: Text.Wrap
                }

                // What « Importer tout » does before anything goes, and that closing the window
                // stops nothing once it has.
                Text {
                    objectName: "import-summary"
                    width: parent.width
                    visible: dialog.phase === dialog.watching
                    text: dialog.unsent ? RecapCaptions.recapFoot : RecapCaptions.sendingFoot
                    textFormat: Text.PlainText
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(13)
                    wrapMode: Text.Wrap
                }

            }

            Row {
                id: buttons

                anchors.right: parent.right
                anchors.rightMargin: Widths.space.card
                anchors.verticalCenter: parent.verticalCenter
                spacing: Widths.space.row

                // The way back, whose word and destination follow the step. Cancel while
                // nothing is chosen; « Vider » over the cards (it gives the preparing up);
                // « Précédent » over the transfers, sent or not, back to the checking step: what
                // is moving keeps moving, and the frame shows it read-only.
                //
                // The same pill as « Importer », drawn as an outline: one leaves, one goes
                // on, and a pair has to look like one before it can say which is which.
                CardAction {
                    id: backAction

                    objectName: "import-back"
                    anchors.verticalCenter: parent.verticalCenter
                    // On its way back once something is being watched: the pickers are gone.
                    focus: dialog.phase === dialog.watching
                    label: dialog.phase === dialog.choosing ? ImportCaptions.cancelLabel
                           : dialog.phase === dialog.watching ? ImportCaptions.previousLabel
                           : dialog.aimKind.length > 0 ? ImportCaptions.anotherFileLabel()
                                                       : ImportCaptions.emptyLabel
                    onTriggered: {
                        if (dialog.phase === dialog.choosing) {
                            dialog.close()
                            return
                        }
                        // Nothing is given up on the way back from watching: what is moving
                        // keeps moving, and the lines stay in the list.
                        if (dialog.phase === dialog.watching) {
                            dialog.openFrame()
                            return
                        }
                        ImportQueue.giveUpPreparing()
                        dialog.phase = dialog.choosing
                    }
                }

                ActionButton {
                    id: startAction

                    objectName: "import-start"
                    anchors.verticalCenter: parent.verticalCenter
                    scaled: true
                    height: sized(28)
                    visible: dialog.phase === dialog.proposing
                    focus: dialog.phase === dialog.proposing
                    /// Nothing is sent from the cards: they only say what was understood, and
                    /// « Importer » is the third step's button — a button here that sent
                    /// straight away let a reader import what they had never looked at.
                    readonly property bool stops: Imports.reviewing > 0
                    // Not on the end of the walk: at fifty series it outlasts the first look,
                    // and a reader sat before a list ready to be read. The frame opens as soon
                    // as one card can go or has stopped, and `importable` is true only of a
                    // card that leaves. What is still read goes in its turn after the press
                    // (`Carrier::tookFolder`), and a card waiting for its tick on a series that
                    // would move is no card that can go.
                    ready: stops || Review.importable || Review.nothingToImport

                    // Hovered while it waits for the count: the one line under the zone says
                    // the same thing, and a button that does not answer should too.
                    LeafToolTip {
                        objectName: "import-start-hint"
                        // The button's own box: its default slot is the glyph's, a box of no
                        // width whose centre would put the bubble beside the button.
                        parent: startAction
                        visible: startAction.hovered && !startAction.ready
                                 && (Imports.foldersToRead > 0 || ImportTally.reading > 0)
                        text: DropCaptions.startWaitsHint
                    }
                    // Nothing dropped is held and nothing goes: it closes, as the frame's button
                    // does. « Rien à importer » here was a button that went on to watch a
                    // transfer of nothing. A drop the library holds whole goes on to the frame
                    // instead, which says so and closes from there: closing here hid it.
                    readonly property bool closes: Review.nothingToImport && !stops
                                                   && !Review.sparesSomething
                    label: closes ? ImportCaptions.closeLabel : ImportCaptions.nextLabel
                    onTriggered: {
                        if (closes) {
                            // Told to the server all the same, so its sessions are cleaned up
                            // rather than left in its inbox (`Imports::send`).
                            ImportQueue.send()
                            dialog.goTo(dialog.choosing)
                            dialog.close()
                            return
                        }
                        dialog.openFrame()
                    }
                }

                // « Importer tout »: every line not moving and not done. It stays on the step
                // while the transfers are watched, for the lines a reader stopped or that failed
                // and for a drop added since; grey when there is nothing left to let go.
                ActionButton {
                    id: sendAction

                    objectName: "import-send"
                    anchors.verticalCenter: parent.verticalCenter
                    scaled: true
                    height: sized(28)
                    visible: dialog.phase === dialog.watching
                    // Once every line has arrived there is nothing to let go: the button is
                    // the way out, and shuts the window on a queue that has been emptied.
                    ready: ImportTally.completed || ImportTally.startable > 0
                    label: ImportTally.completed ? ImportCaptions.finishLabel
                                             : ImportCaptions.sendLabel
                    onTriggered: {
                        if (ImportTally.completed)
                            dialog.close()
                        else
                            ImportQueue.startAll()
                    }
                }
            }
        }
    }

    /// Opened from the bar, which knows nothing of phases: the dialog picks the one that
    /// answers the question being asked. Something moving means « show me where it is »;
    /// nothing moving means « let me drop something ».
    function show() {
        aimKind = ""
        aimVolumes = 0
        // A card stopped to be checked is a question as surely as a tick is: opened on the
        // transfers, the window showed a folder nobody had sent as if it were moving.
        //
        // Only what « Importer » let go is a transfer to watch. The window used to jump to
        // the transfers as soon as nothing was read or asked any more, so a reading that
        // ended while it was shut (27 gigabytes checked in the background) reopened it on the
        // third step for a reader who had left it on the first. Without a transfer, it
        // reopens where it was left.
        if (ImportTally.launched > 0 && Imports.deciding === 0 && ImportTally.reading === 0
                && Imports.reviewing === 0)
            phase = watching
        else if (Imports.count > 0 && (phase === choosing || ImportTally.launched > 0))
            phase = proposing
        open()
    }

    /// Opened from the menu of a volume or a series: the drop zone expects what is aimed at
    /// and whatever is dropped is aimed at it. Cards of a plain import still being prepared
    /// are given up first — they would be checked beside a target they do not aim at.
    function showFor(kind, id, name, volumes = 0) {
        if (Imports.count > 0 && aimKind.length === 0)
            ImportQueue.giveUpPreparing()
        aimKind = kind
        aimId = id
        aimName = name
        aimVolumes = volumes
        phase = choosing
        open()
    }

    /// Opens the checking step on what the queue holds.
    function openFrame() {
        Review.view.open()
        goTo(verifying)
    }

    /// Whatever a picker or a drop handed over. Turning URLs into local paths is the
    /// model's job: what is not a local file is dropped on the floor there rather than
    /// sent on as a path no server could open.
    function take(urls) {
        const taken = aimKind.length > 0 ? ImportOffers.reimportUrls(aimKind, aimId, aimName, urls)
                                         : ImportOffers.offerUrls(urls)
        if (taken > 0)
            phase = proposing
    }
}
