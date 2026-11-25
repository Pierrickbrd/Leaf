// What one file says about itself, corrected: its number, its title, its ISBN, when it came
// out, who wrote and drew it, its summary. Saving rewrites the archive — the `entry.json` inside it — and the window
// says so before it is asked to, since nothing else on the screen tells that a click touches
// a file on the disk.
//
// Only what changed is sent; a line emptied takes the field away. The number can be changed
// and not taken away: a volume without one is not a volume the edition can place. A number
// that is not a number and a date that does not exist are said under their fields as they are
// typed, and the button waits.
//
// A credit the volume does not give is its series' — what `work.json` declares —, shown greyed
// in the field and never sent: typing a name gives the volume a list of its own, which replaces
// the series' whole, and taking its last name away sends an empty list, the series' word again.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Leaf

LeafDialog {
    id: dialog

    objectName: "entry-edit"
    surfaceName: "entry-edit-surface"
    drawnWidth: 600
    initialFocus: numberLine.field

    readonly property var words: Editing.words
    property string entryId: ""
    property string seriesId: ""
    property string heading: ""
    property var loaded: ({})
    /// What the library already uses, to suggest from.
    property var vocabulary: ({})
    /// The series' declared credits, once read: `{ authors, artists }`. Until then, the names
    /// the volume itself draws from it, which are the same when it draws on them.
    property var defaults: ({})

    function show(entry, series, label) {
        dialog.entryId = entry
        dialog.seriesId = series
        dialog.heading = label
        dialog.loaded = Entries.forEditing(entry)
        numberLine.text = dialog.loaded.number ?? ""
        titleLine.text = dialog.loaded.title ?? ""
        isbnLine.text = dialog.loaded.isbn ?? ""
        publishedLine.load(dialog.loaded.publishedOn ?? "")
        covering.reset()
        summary.text = dialog.loaded.summary ?? ""
        authorsLine.words = dialog.loaded.authors ?? []
        artistsLine.words = dialog.loaded.artists ?? []
        dialog.defaults = ({})
        Editing.loadSeries(series)
        Editing.loadVocabulary()
        dialog.open()
    }

    function orNull(text) {
        const trimmed = text.trim()
        return trimmed.length > 0 ? trimmed : null
    }

    function changes() {
        const sent = ({})
        const was = dialog.loaded
        const text = (key, now) => {
            if (now.trim() !== (was[key] ?? ""))
                sent[key] = dialog.orNull(now)
        }
        // « 2,5 » is typed as French writes it and sent as the contract does.
        if (dialog.numberProblem.length === 0 && numberLine.text.trim() !== (was.number ?? ""))
            sent.number = Number(numberLine.text.trim().replace(",", "."))
        text("title", titleLine.text)
        text("isbn", isbnLine.text)
        if (publishedLine.value !== publishedLine.initial)
            sent.publishedOn = dialog.orNull(publishedLine.value)
        text("summary", summary.text)
        // An own list sent only when it moved; emptied, it is sent empty, which is the series'.
        for (const [key, line] of [["authors", authorsLine], ["artists", artistsLine]]) {
            if (line.words.join(",") !== (was[key] ?? []).join(","))
                sent[key] = line.words
        }
        // Counted from 0 as the contract does; the window speaks from 1. A page chosen again is
        // not sent again.
        if (covering.pickedPage >= 0
                && covering.pickedPage + 1 !== parseInt(was.coverPage ?? "0", 10))
            sent.coverPage = covering.pickedPage
        return sent
    }

    // `parseFloat` read « 2abc » as 2 and sent it: the whole of what is typed must be a number.
    readonly property string numberProblem: {
        const typed = numberLine.text.trim()
        if (typed.length === 0)
            return dialog.words.numberMissing
        return /^-?\d+([.,]\d+)?$/.test(typed) ? "" : dialog.words.numberInvalid
    }
    readonly property bool correct: dialog.numberProblem.length === 0
                                    && publishedLine.problem.length === 0

    function submit() {
        if (!dialog.correct || Commands.busy)
            return
        const sent = dialog.changes()
        if (Object.keys(sent).length === 0 && !covering.changed) {
            dialog.close()
            return
        }
        Commands.editEntry(dialog.entryId, dialog.seriesId, sent, covering.cover())
    }

    Connections {
        target: Commands

        function onEdited(seriesId, entryId) {
            if (entryId === dialog.entryId && entryId.length > 0)
                dialog.close()
        }
    }

    Connections {
        target: Editing

        function onSeriesLoaded(series) {
            if (series.id === dialog.seriesId)
                dialog.defaults = ({ "authors": series.authors, "artists": series.artists })
        }

        function onVocabularyLoaded(vocabulary) {
            dialog.vocabulary = vocabulary
        }
    }

    Text {
        width: parent.width
        text: dialog.words.entry + (dialog.heading.length > 0 ? " — " + dialog.heading : "")
        color: Theme.ink
        font.family: Theme.displayFamily
        font.pixelSize: Widths.scale.title(20)
        font.weight: Font.Bold
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
    }

    Text {
        objectName: "entry-edit-warning"
        width: parent.width
        textFormat: Text.PlainText
        text: dialog.words.rewrites
        color: Theme.alert
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.body(13)
        wrapMode: Text.WordWrap
    }

    // The cover at the left on the line of the title, then the short fields on a grid of three
    // equal columns, then the summary: the series' window is laid out the same way. A single
    // page has nothing to pick among, so only the image is offered.
    Row {
        width: parent.width
        spacing: Widths.space.compact + Widths.space.compact

        CoverChoice {
            id: covering

            objectName: "entry-edit-cover"
            prefix: "entry-edit-cover"
            width: (parent.width - parent.spacing) * 0.45
            words: dialog.words
            current: dialog.loaded.cover ?? ""
            kept: (dialog.loaded.coverImage ?? false) || (dialog.loaded.coverPage ?? "").length > 0
            keptPage: parseInt(dialog.loaded.coverPage ?? "0", 10) || 0
            pages: true
            pagesUrl: dialog.loaded.pages ?? ""
            pageCount: dialog.loaded.pageCount ?? 0
            revertLabel: dialog.words.backToDefault
            defaultStatus: dialog.words.coverDefaultVolume
        }
        EditField {
            id: titleLine

            objectName: "entry-edit-title"
            width: parent.width - covering.width - parent.spacing
            name: dialog.words.title
        }
    }

    Row {
        width: parent.width
        spacing: Widths.space.compact

        EditField {
            id: numberLine

            objectName: "entry-edit-number"
            width: (parent.width - 2 * parent.spacing) / 3
            name: dialog.words.number
            problem: dialog.numberProblem
        }
        // One cell, so that whatever the date field becomes is a swap of this item alone.
        Item {
            width: (parent.width - 2 * parent.spacing) / 3
            height: publishedLine.height

            DateField {
                id: publishedLine

                objectName: "entry-edit-published"
                width: parent.width
                name: dialog.words.publishedOn
                words: dialog.words
            }
        }
        EditField {
            id: isbnLine

            objectName: "entry-edit-isbn"
            width: (parent.width - 2 * parent.spacing) / 3
            name: dialog.words.isbn
        }
    }

    TokenField {
        id: authorsLine

        objectName: "entry-edit-authors"
        name: dialog.words.authors
        hint: dialog.words.list
        createLabel: dialog.words.create
        vocabulary: dialog.vocabulary.authors ?? []
        inherited: dialog.defaults.authors ?? dialog.loaded.seriesAuthors ?? []
    }
    TokenField {
        id: artistsLine

        objectName: "entry-edit-artists"
        name: dialog.words.artists
        hint: dialog.words.list
        createLabel: dialog.words.create
        vocabulary: dialog.vocabulary.artists ?? []
        inherited: dialog.defaults.artists ?? dialog.loaded.seriesArtists ?? []
    }

    Column {
        width: parent.width
        spacing: Widths.space.compact

        Text {
            textFormat: Text.PlainText
            text: dialog.words.summary
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            font.weight: Font.Medium
        }

        Rectangle {
            width: parent.width
            height: Widths.scale.size(72)
            radius: 6
            color: Theme.onBar
            border.color: summary.activeFocus ? Theme.emerald : "transparent"

            Flickable {
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                contentHeight: summary.implicitHeight
                boundsBehavior: Flickable.StopAtBounds

                TextArea.flickable: TextArea {
                    id: summary

                    objectName: "entry-edit-summary"
                    activeFocusOnTab: true
                    wrapMode: TextEdit.Wrap
                    color: Theme.ink
                    selectionColor: Theme.emerald
                    selectedTextColor: Theme.onEmerald
                    font.family: Theme.textFamily
                    font.pixelSize: 14
                    background: null
                }
            }
        }
    }

    commands: [
        LeafTextAction {
            objectName: "entry-edit-cancel"
            scaled: true
            label: dialog.words.cancel
            activeFocusOnTab: true
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: dialog.close()
        },

        ActionButton {
            objectName: "entry-edit-save"
            scaled: true
            label: dialog.words.save
            ready: dialog.correct && !Commands.busy
            onTriggered: dialog.submit()
        }
    ]
}
