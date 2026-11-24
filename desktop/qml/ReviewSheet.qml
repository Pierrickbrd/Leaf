// The checking frame's sheet, on its right: what was last clicked — a node or a line — with
// each of its values and where each came from, and under them what has to be said aloud.
//
// The sheet follows the last click, as the tree and the list hand it on (`Review.view.select`).
// Its fields are drawn by count, for the reason `ReviewRail` gives: a value typed rebuilds
// `Review.sheet`, and a field rebuilt is a field that loses the keys a Tab just gave it.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import Leaf

Item {
    id: panel

    /// Read once and only while the frame is shown, for the reason `ReviewList` gives.
    readonly property var sheet: panel.visible ? Review.sheet : ({})
    readonly property var allFields: panel.sheet.fields === undefined ? [] : panel.sheet.fields
    /// The series aimed at lists the values that differ as pairs, not as fields to type.
    readonly property bool pairs: panel.allFields.length > 0
                                  && panel.allFields[0].arriving !== undefined
    // The series' writer among the other fields, just above its drawer: drawn apart, beside
    // whom the volumes credit, « Scénario » sat under « Étiquettes », away from « Dessin ».
    readonly property var fields: panel.pairs ? [] : panel.allFields
    readonly property var alarming: panel.sheet.alarming ?? ({})
    readonly property var progress: panel.sheet.progress ?? ({})
    readonly property var notes: panel.sheet.notes === undefined ? [] : panel.sheet.notes
    /// The frame is a look at what already went: readings and places are shown, not chosen.
    property bool readOnly: false
    /// An archive at a universe's root: what it says is why it has to be given a place, and
    /// is said first, over the choices — pinned under them, the sentence that it will not be
    /// read came after the answer to it. The same for a card refused: its reason pinned under
    /// a sheet of read-only fields was a page of values with nothing saying why none could be
    /// touched.
    readonly property bool notesFirst: (panel.sheet.place ?? []).length > 0
                                       || panel.sheet.removable === true
    readonly property bool readingsShown: panel.visible && Review.railHint.length > 0
    /// What a box keeps clear on each side for the ring round it.
    readonly property int ringRoom: Theme.focusGap + 2

    /// The keys were on something of the sheet that is no longer drawn.
    signal keysLost()
    /// A reading order of the universe shown asked to be opened, from its line.
    signal orderOpening(string orderId)

    /// The keys to the line of the order `orderId`; false when the sheet draws none.
    function focusOrder(orderId: string): bool {
        return orders.focusOrder(orderId)
    }

    clip: true

    Item {
        id: heading

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Widths.space.card
        anchors.bottomMargin: 0
        height: title.implicitHeight

        Text {
            id: title

            objectName: "review-sheet-title"
            anchors.left: parent.left
            anchors.right: subtitle.left
            anchors.rightMargin: Widths.space.label
            anchors.top: parent.top
            text: panel.sheet.title === undefined ? "" : panel.sheet.title
            textFormat: Text.PlainText
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: Widths.scale.title(26)
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        // On the title's baseline: a caption beside a heading.
        Text {
            id: subtitle

            objectName: "review-sheet-subtitle"
            anchors.right: parent.right
            anchors.baseline: title.baseline
            width: Math.min(implicitWidth, heading.width * 0.55)
            text: panel.sheet.subtitle === undefined ? "" : panel.sheet.subtitle
            textFormat: Text.PlainText
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
        }
    }

    Flickable {
        id: flick

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.bottom: said.top
        anchors.topMargin: Widths.space.row
        anchors.leftMargin: Widths.space.card
        anchors.rightMargin: Widths.space.card
        anchors.bottomMargin: said.height > 0 ? Widths.space.row : 0
        contentHeight: column.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: column

            width: flick.width
            spacing: 0

            Column {
                width: column.width
                visible: panel.notesFirst
                spacing: Widths.space.compact
                bottomPadding: Widths.space.compact

                Repeater {
                    model: panel.notesFirst ? panel.notes.length : 0
                    delegate: noteBox
                }
            }

            // What a reimport finds wrong with a file, in the alert's wash, over the rest.
            Rectangle {
                objectName: "review-alarming"
                width: column.width
                visible: panel.alarming.title !== undefined && panel.alarming.title.length > 0
                height: visible ? alarmingWords.implicitHeight + 2 * Widths.space.row : 0
                radius: Widths.scale.size(12)
                color: Theme.alertWash

                Column {
                    id: alarmingWords

                    x: Widths.space.row
                    y: Widths.space.row
                    width: parent.width - 2 * Widths.space.row
                    spacing: 2

                    Text {
                        width: parent.width
                        textFormat: Text.PlainText
                        text: panel.alarming.title ?? ""
                        color: Theme.alert
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(13)
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        width: parent.width
                        textFormat: Text.PlainText
                        text: panel.alarming.text ?? ""
                        color: Theme.inkSoft
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.caption(12)
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // The cover of the series or the edition: the one its folder holds, or the one
            // chosen from the disk, and the way to change it before anything is sent. The
            // file chosen is announced as `cover.<extension>` in that folder, which is the
            // name the server reads a cover by.
            Row {
                id: cover

                readonly property var said: panel.sheet.cover ?? ({})
                objectName: "review-cover"
                width: column.width
                visible: cover.said.at !== undefined
                spacing: Widths.space.card
                bottomPadding: visible ? Widths.space.row : 0

                Item {
                    width: Widths.scale.fit(72)
                    height: Widths.scale.fit(108)

                    RoundedCover {
                        objectName: "review-cover-image"
                        anchors.fill: parent
                        visible: (cover.said.url ?? "").length > 0
                        source: cover.said.url ?? ""
                        radius: Widths.scale.size(6)
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: (cover.said.url ?? "").length === 0
                        radius: Widths.scale.size(6)
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.rule

                        Text {
                            anchors.fill: parent
                            anchors.margins: Widths.space.compact
                            textFormat: Text.PlainText
                            text: Editing.words.noCover
                            color: Theme.inkSoft
                            font.family: Theme.textFamily
                            font.pixelSize: Widths.scale.caption(12)
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Column {
                    spacing: Widths.space.compact
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        textFormat: Text.PlainText
                        text: Editing.words.cover
                        color: Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(13)
                        font.weight: Font.DemiBold
                    }

                    CardAction {
                        objectName: "review-cover-change"
                        visible: cover.said.editable === true && !panel.readOnly
                        activeFocusOnTab: visible
                        label: Editing.words.changeCover
                        onTriggered: coverFinder.open()
                    }

                    CardAction {
                        objectName: "review-cover-revert"
                        visible: cover.said.picked === true && !panel.readOnly
                        activeFocusOnTab: visible
                        label: Editing.words.backToDefault
                        onTriggered: Review.edits.setCover(cover.said.at, "")
                    }
                }
            }

            ReviewChoices {
                x: panel.ringRoom
                width: column.width - 2 * panel.ringRoom
                choices_: panel.sheet.choices ?? []
                about: panel.sheet.id ?? ""
                topPadding: visible ? Widths.space.compact : 0
                bottomPadding: visible ? Widths.space.compact : 0
            }

            ReviewSplice {
                width: column.width
                splice_: panel.sheet.splice ?? ({})
                topPadding: visible ? Widths.space.compact : 0
                bottomPadding: visible ? Widths.space.compact : 0
            }

            ReviewCompare {
                width: column.width
                compare: panel.sheet.compare ?? ({})
                topPadding: visible ? Widths.space.compact : 0
                bottomPadding: visible ? Widths.space.compact : 0
            }

            // Whose place in the volume this is about, and — when the pages change by much —
            // the question of keeping it or starting again.
            Column {
                objectName: "review-progress"
                x: panel.ringRoom
                width: column.width - 2 * panel.ringRoom
                visible: (panel.progress.text ?? "").length > 0
                spacing: Widths.space.compact
                bottomPadding: visible ? Widths.space.compact : 0

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: panel.progress.text ?? ""
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(13)
                    wrapMode: Text.WordWrap
                }

                ReviewChoice {
                    objectName: "review-progress-keep"
                    width: parent.width
                    visible: panel.progress.ask === true
                    label: panel.progress.keepLabel ?? ""
                    chosen: panel.progress.reset !== true
                    onPicked: Review.edits.setReset(panel.sheet.id, false)
                }

                ReviewChoice {
                    objectName: "review-progress-reset"
                    width: parent.width
                    visible: panel.progress.ask === true
                    label: panel.progress.resetLabel ?? ""
                    chosen: panel.progress.reset === true
                    onPicked: Review.edits.setReset(panel.sheet.id, true)
                }
            }

            ReviewKeep {
                x: panel.ringRoom
                width: column.width - 2 * panel.ringRoom
                pairs: panel.pairs ? panel.allFields : []
            }

            // The boxes are inset by the ring's room: the list clips at its edge, and a ring
            // drawn round a box as wide as it lost its two sides.
            ReviewPlace {
                x: panel.ringRoom
                width: column.width - 2 * panel.ringRoom
                sheet: panel.sheet
                readOnly: panel.readOnly
                bottomPadding: visible ? Widths.space.compact : 0
            }

            // Only once a role in the frame is a proposal, as the tree's chips: a drop whose
            // every role is said or certain has nothing to choose between, and its sheet kept
            // the fields it always had, under a reading nobody asked about.
            ReviewReadings {
                visible: panel.readingsShown && (readings.length > 0 || clues.length > 0
                                                 || writes.length > 0)
                x: panel.ringRoom
                width: column.width - 2 * panel.ringRoom
                sheet: panel.sheet
                readOnly: panel.readOnly
                bottomPadding: visible ? Widths.space.compact : 0
                onLost: panel.keysLost()
            }

            Repeater {
                model: panel.fields.length

                delegate: ReviewField {
                    required property int index

                    width: column.width
                    height: implicitHeight
                    // As `ReviewRail`'s node: read once more past the end of a sheet that shrank.
                    field: panel.fields[index] ?? ({ key: "", label: "", text: "", lacking: false,
                                                     placeholder: "",
                                                     choices: [], editable: false, doubt: "" })
                    sheetId: panel.sheet.id === undefined ? "" : panel.sheet.id
                    sheet: panel.sheet
                }
            }

            ReviewCredits {
                width: column.width
                sheet: panel.sheet
            }

            ReviewArcs {
                width: column.width
                sheet: panel.sheet
            }

            ReviewOrders {
                id: orders

                width: column.width
                sheet: panel.sheet
                onOpening: orderId => panel.orderOpening(orderId)
            }
        }
    }

    FollowsFocus {
        flick: flick
    }

    // What the sheet says aloud — the reading direction nobody said, an archive nobody could
    // open — pinned under the fields rather than after them, so that a sheet taller than the
    // window still says it.
    Column {
        id: said

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Widths.space.card
        spacing: Widths.space.compact

        Repeater {
            model: panel.notesFirst ? 0 : panel.notes.length
            delegate: noteBox
        }

        // What the library holds of the volume or the series aimed at, corrected from here: a
        // reimport writes nothing of what is identical, and the frame was the one place where
        // the information could be seen and not changed. The windows are the library's own
        // (`EntryEditDialog`, `SeriesEditDialog`) and rewrite its files at once, not at
        // « Appliquer »: they are offered to the profiles that may edit, as everywhere.
        CardAction {
            objectName: "review-edit-information"
            visible: Profiles.canEdit && (panel.sheet.editSeriesId ?? "").length > 0
            activeFocusOnTab: visible
            label: ReviewCaptions.editLabel
            onTriggered: {
                editor.active = true
                if ((panel.sheet.editEntryId ?? "").length > 0)
                    editor.item.show(panel.sheet.editEntryId, panel.sheet.editSeriesId,
                                     panel.sheet.title ?? "")
                else
                    editor.item.show(panel.sheet.editSeriesId)
            }
        }

        // Under the reason a card is refused, the one thing left to do with it: nothing on
        // its sheet can be corrected, and left in the queue it would wait there for ever.
        CardAction {
            objectName: "review-remove"
            visible: panel.sheet.removable === true && !panel.readOnly
            activeFocusOnTab: visible
            alarming: true
            label: ImportCaptions.removeLabel
            onTriggered: {
                if (Review.edits.remove(panel.sheet.id ?? ""))
                    panel.keysLost()
            }
        }
    }

    // Built the first time it is asked for, as the command menu's: a window with a dozen
    // fields is not something a frame that nobody edits from should carry.
    Loader {
        id: editor

        active: false
        sourceComponent: (panel.sheet.editEntryId ?? "").length > 0 ? entryEditor : seriesEditor
    }

    FileDialog {
        id: coverFinder

        objectName: "review-cover-dialog"
        title: Editing.words.chooseImageTitle
        nameFilters: [Editing.words.imageFiles]
        onAccepted: Review.edits.setCover(panel.sheet.cover?.at ?? "", coverFinder.selectedFile.toString())
    }

    Component { id: seriesEditor; SeriesEditDialog {} }
    Component { id: entryEditor; EntryEditDialog {} }

    // One note, drawn the same whether it is pinned under the fields or said over the
    // choices of an archive.
    Component {
        id: noteBox

        Rectangle {
            id: note

            required property int index
            readonly property var said: panel.notes[index] ?? ({ text: "", alarming: false })
            readonly property bool alarming: note.said.alarming === true

            objectName: "review-sheet-note-" + index
            width: parent.width
            height: words.implicitHeight + 2 * Widths.space.row
            radius: Widths.scale.size(10)
            color: note.alarming ? Theme.alertWash : Theme.onPaper
            // The quiet note is edged in the rule; the alarming one is its wash alone, an
            // edge of the same colour as its fill being no edge at all.
            border.color: Theme.rule
            border.width: note.alarming ? 0 : 1

            Text {
                id: words

                objectName: note.objectName + "-text"
                anchors.fill: parent
                anchors.margins: Widths.space.row
                text: note.said.text
                textFormat: Text.PlainText
                color: note.alarming ? Theme.alert : Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                font.weight: note.alarming ? Font.Medium : Font.Normal
                lineHeight: 1.3
                wrapMode: Text.WordWrap

                Accessible.role: note.alarming ? Accessible.AlertMessage : Accessible.StaticText
                Accessible.name: text
            }
        }
    }
}
