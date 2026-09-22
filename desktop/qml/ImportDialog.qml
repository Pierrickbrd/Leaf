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

    /// Which of the three the dialog is showing. Not derived from the queue: a reader who
    /// stepped back to phase one must stay there while a transfer runs behind them.
    property int phase: choosing

    readonly property int choosing: 0
    readonly property int proposing: 1
    readonly property int watching: 2

    /// Whether anything is still being set up — what the × would throw away.
    readonly property bool preparing: phase !== watching && Imports.count > 0

    objectName: "import-dialog"
    width: Math.min(620, Overlay.overlay ? Overlay.overlay.width - 80 : 620)
    height: Math.min(560, Overlay.overlay ? Overlay.overlay.height - 80 : 560)
    anchors.centerIn: Overlay.overlay
    padding: 0
    modal: true
    focus: true
    // Escape is the × — it means the same thing and a reader reaches for it first.
    closePolicy: Popup.CloseOnEscape

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
    Connections {
        target: Imports

        function onChanged() {
            if (dialog.phase === dialog.proposing && Imports.count === 0)
                dialog.phase = dialog.choosing
        }
    }

    contentItem: Item {
        // ——— The heading, and the way out ———————————————————————————————————

        Item {
            id: heading

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 56

            Text {
                objectName: "import-title"
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                text: ImportCaptions.title
                color: Theme.ink
                font.family: Theme.displayFamily
                font.pixelSize: 19
                font.weight: Font.Bold
            }

            BarButton {
                objectName: "import-close"
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                source: "assets/icons/close.svg"
                label: ImportCaptions.cancelLabel
                // The cross hides the window. It does not decide anything.
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
            anchors.right: parent.right
            anchors.top: heading.bottom
            anchors.bottom: verify.top
            anchors.margins: 20
            anchors.bottomMargin: 16
            visible: dialog.phase === dialog.choosing
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
                    if (well.containsDrag) {
                        ink.fillStyle = Theme.emeraldWash
                        ink.beginPath()
                        ink.roundedRect(0, 0, width, height, Theme.cardRadius,
                                        Theme.cardRadius)
                        ink.fill()
                    }
                    ink.strokeStyle = well.containsDrag ? Theme.emerald : Theme.rule
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
                anchors.centerIn: parent
                spacing: 14

                Text {
                    objectName: "import-well-words"
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: ImportCaptions.dropHereLabel
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 15
                }

                Text {
                    objectName: "import-well-how"
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: ImportCaptions.dropHowLabel
                    color: Theme.inkFaint
                    font.family: Theme.textFamily
                    font.pixelSize: 13
                }

                // Two pickers and not one, because the desktop has two: `FileDialog` cannot
                // return a folder and `FolderDialog` cannot return a file, and a single
                // button would do half the job without saying which half.
                //
                // The verb is said once, in faint ink, and the two are its objects — so they
                // read as one idea with two doors rather than as two unrelated commands
                // competing beneath the invitation to drop.
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 4

                    Text {
                        objectName: "import-choose-lead"
                        anchors.verticalCenter: parent.verticalCenter
                        rightPadding: 4
                        text: ImportCaptions.chooseLeadLabel
                        color: Theme.inkFaint
                        font.family: Theme.textFamily
                        font.pixelSize: 13
                    }

                    LeafTextAction {
                        objectName: "import-choose-files"
                        anchors.verticalCenter: parent.verticalCenter
                        label: ImportCaptions.chooseFilesLabel
                        onTriggered: {
                            if (Preferences.lastPlace != "")
                                files.currentFolder = Preferences.lastPlace
                            files.open()
                        }
                    }

                    LeafTextAction {
                        objectName: "import-choose-folder"
                        anchors.verticalCenter: parent.verticalCenter
                        label: ImportCaptions.chooseFolderLabel
                        onTriggered: {
                            if (Preferences.lastPlace != "")
                                folder.currentFolder = Preferences.lastPlace
                            folder.open()
                        }
                    }
                }
            }

        }

        // Under the dashed box and not inside it. The dashes mean « put something here »,
        // and a setting enclosed by them reads as part of the target rather than as how
        // what lands in it will be read. Dropping onto it still works: the window itself
        // is a drop area — see `Main.qml`.
        //
        // Read when a folder is taken in, never after: the fingerprints are computed while
        // the manifest is built, and a session announced without them cannot grow them
        // afterwards. So it belongs in the phase before anything is chosen, and it is gone
        // from the two that follow.
        LeafCheck {
            id: verify

            objectName: "import-verify"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: commands.top
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.bottomMargin: 18
            visible: dialog.phase === dialog.choosing
            label: ImportCaptions.verifyLabel
            aside: ImportCaptions.verifyAside
            checked: Imports.verifying
            onToggled: wanted => Imports.verifying = wanted
        }

        // Both pickers open where they were last used. A library lives in one place and a
        // reader imports from it over and over; opening on the home folder every time meant
        // walking the same four levels down before every single import. `currentFolder` is
        // left alone when nothing has been remembered yet, so the first use keeps whatever
        // the desktop hands over.
        FileDialog {
            id: files

            objectName: "import-file-picker"
            title: ImportCaptions.chooseFilesTitle
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
            title: ImportCaptions.chooseFolderTitle
            onAccepted: {
                // The folder it sits in, not the folder itself: a reader who imported
                // « Bleach » is far more likely to want « Dragon Ball » next than to want
                // to look inside « Bleach ».
                Preferences.rememberPlace(currentFolder)
                dialog.take([selectedFolder])
            }
        }

        // ——— Phases two and three: one row per file —————————————————————————

        ListView {
            id: rows

            objectName: "import-rows"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: heading.bottom
            anchors.bottom: commands.top
            anchors.margins: 16
            visible: dialog.phase !== dialog.choosing
            clip: true
            spacing: 2
            model: Imports
            boundsBehavior: Flickable.StopAtBounds

            delegate: ImportRow {
                required property int index

                row: index
                width: rows.width
                onAnswered: seriesId => Imports.decide(index, seriesId)
                onAgreed: Imports.accept(index)
                onWantsFiling: (workId, wanted) => Imports.setFiling(index, workId, wanted)
                onHeld: Imports.pause(index)
                onReleased: Imports.resume(index)
                onDropped: Imports.abandon(index)
                onUnfolded: at => Imports.toggle(index, at)
                onReplaced: (at, wanted) => Imports.setReplacing(index, at, wanted)
                onRedeclared: (path, wanted) => Imports.setDeclaring(index, path, wanted)
            }
        }

        // ——— What the queue is doing as a whole ——————————————————————————————

        Item {
            id: commands

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 64

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.rule
            }

            Text {
                objectName: "import-trouble"
                anchors.left: parent.left
                anchors.leftMargin: 20
                anchors.right: buttons.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                visible: text.length > 0
                text: Imports.trouble
                color: Theme.alert
                font.family: Theme.textFamily
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            Row {
                id: buttons

                anchors.right: parent.right
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12

                // One button, two words: it cancels in the first phase and steps back in
                // the others, which is what was asked for and what a reader expects of the
                // thing sitting beside "next".
                // The same pill as `Importer`, drawn as an outline. It was a bare word beside
                // a pill: two shapes that share a row and nothing else, sitting on different
                // baselines because one had a height and the other had a line. They are a
                // pair — one leaves, one goes on — and a pair has to look like one before it
                // can say which is which.
                // In the watching phase it goes the other way: back to the drop zone, so a
                // second import can be started without closing the transfers being watched.
                CardAction {
                    objectName: "import-back"
                    anchors.verticalCenter: parent.verticalCenter
                    label: dialog.phase === dialog.choosing ? ImportCaptions.cancelLabel
                           : dialog.phase === dialog.watching ? ImportCaptions.dropSomethingElseLabel
                                                              : ImportCaptions.backLabel
                    onTriggered: {
                        if (dialog.phase === dialog.choosing) {
                            dialog.close()
                            return
                        }
                        // Nothing is given up on the way back from watching: what is moving
                        // keeps moving, and the drop zone is simply the other thing this
                        // window does.
                        if (dialog.phase !== dialog.watching)
                            Imports.giveUpPreparing()
                        dialog.phase = dialog.choosing
                    }
                }

                ActionButton {
                    objectName: "import-start"
                    anchors.verticalCenter: parent.verticalCenter
                    height: 28
                    visible: dialog.phase === dialog.proposing
                    // Nothing goes up while a question is unanswered: a file sailing past
                    // one nobody was asked is the defect the two stages exist to prevent.
                    // And nothing goes up while something is still being looked at, or the
                    // queue finds nothing ready, starts nothing, and the dialog closes on a
                    // transfer that never began.
                    ready: Imports.deciding === 0 && Imports.reading === 0
                           && Imports.count > 0
                    label: ImportCaptions.startLabel
                    onTriggered: {
                        // Stays open on what it just started. It used to close, so the one
                        // moment a reader most wants to watch — the transfer they have just
                        // agreed to — was the moment the window took itself away, and
                        // getting back to it meant finding the button in the bar again.
                        Imports.send()
                        dialog.phase = dialog.watching
                    }
                }
            }
        }
    }

    /// Opened from the bar, which knows nothing of phases: the dialog picks the one that
    /// answers the question being asked. Something moving means « show me where it is »;
    /// nothing moving means « let me drop something ».
    function show() {
        if (Imports.count > 0 && Imports.deciding === 0 && Imports.reading === 0)
            phase = watching
        else if (Imports.count > 0)
            phase = proposing
        open()
    }

    /// Whatever a picker or a drop handed over. Turning URLs into local paths is the
    /// model's job: what is not a local file is dropped on the floor there rather than
    /// sent on as a path no server could open.
    function take(urls) {
        if (Imports.offerUrls(urls) > 0)
            phase = proposing
    }
}
