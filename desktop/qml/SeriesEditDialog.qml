// What the files say about one series, corrected: its title, its words, who made it, what it
// is. Only what was changed is sent, and a field emptied is taken away (`null`) rather than
// left as it was — a value that can only be replaced cannot be corrected.
//
// **The summary is one field and two places.** An edition's own words are kept in its own
// file, and the work's sentence is common to all its editions: this edits whichever the page
// shows, and says which, so that the work's sentence is never copied into an edition.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Leaf

LeafDialog {
    id: dialog

    objectName: "series-edit"
    surfaceName: "series-edit-surface"
    drawnWidth: 720
    initialFocus: titleLine.field

    readonly property var words: Editing.words
    property string seriesId: ""
    /// What the series was when the window opened: a field is sent only if it differs.
    property var loaded: ({})
    property bool ready: false
    /// What the library already uses, to suggest from: `{ authors, genres, tags… }`.
    property var vocabulary: ({})
    property int status: -1
    property int medium: -1
    /// The volumes that can be the cover, `[{ number, label, cover }]`; `null` until known.
    property var volumes: null

    function show(id) {
        dialog.seriesId = id
        dialog.ready = false
        dialog.loaded = ({})
        dialog.volumes = null
        covering.reset()
        Editing.loadSeries(id)
        Editing.loadVolumes(id)
        Editing.loadVocabulary()
        dialog.open()
    }

    /// The text, or `null` when it is blank: the way a field is taken away.
    function orNull(text) {
        const trimmed = text.trim()
        return trimmed.length > 0 ? trimmed : null
    }

    function changes() {
        const sent = ({})
        const was = dialog.loaded
        const text = (key, line) => {
            if (line.text.trim() !== (was[key] ?? ""))
                sent[key] = dialog.orNull(line.text)
        }
        text("title", titleLine)
        text("ageRating", ageLine)
        const singles = [["publisher", publisherLine], ["collection", collectionLine],
                         ["language", languageLine]]
        for (const [key, line] of singles) {
            const now = line.words[0] ?? ""
            if (now !== (was[key] ?? ""))
                sent[key] = dialog.orNull(now)
        }
        if (summary.text.trim() !== (was.summary ?? ""))
            sent[was.ownSummary ? "editionSummary" : "summary"] = dialog.orNull(summary.text)
        const lists = [["authors", authorsLine], ["artists", artistsLine],
                       ["genres", genresLine], ["tags", tagsLine]]
        for (const [key, line] of lists) {
            const now = line.words
            if (now.join(",") !== (was[key] ?? []).join(","))
                sent[key] = now
        }
        const count = countLine.text.trim()
        if (count !== (was.volumeCount > 0 ? String(was.volumeCount) : ""))
            sent.volumeCount = count.length > 0 ? parseInt(count, 10) : null
        if (dialog.status !== was.status)
            sent.status = dialog.status < 0 ? null : ["ongoing", "completed", "hiatus", "cancelled"][dialog.status]
        if (dialog.medium !== was.medium) {
            const names = ["manga", "bd", "comics", "manhwa", "manhua", "webtoon", "artbook",
                           "other"]
            sent.medium = dialog.medium < 0 ? null : names[dialog.medium]
        }
        // A volume chosen travels with the fields; an image, or going back, with the cover.
        if (covering.pickedVolume !== null)
            sent.coverVolume = covering.pickedVolume
        return sent
    }

    function submit() {
        if (!dialog.ready || Commands.busy)
            return
        const sent = dialog.changes()
        if (Object.keys(sent).length === 0 && !covering.changed) {
            dialog.close()
            return
        }
        Commands.editSeries(dialog.seriesId, sent, covering.cover())
    }

    Connections {
        target: Editing

        function onSeriesLoaded(series) {
            if (series.id !== dialog.seriesId)
                return
            dialog.loaded = series
            titleLine.text = series.title
            summary.text = series.summary
            authorsLine.words = series.authors
            artistsLine.words = series.artists
            genresLine.words = series.genres
            tagsLine.words = series.tags
            publisherLine.words = series.publisher.length > 0 ? [series.publisher] : []
            collectionLine.words = series.collection.length > 0 ? [series.collection] : []
            languageLine.words = series.language.length > 0 ? [series.language] : []
            ageLine.text = series.ageRating
            countLine.text = series.volumeCount > 0 ? String(series.volumeCount) : ""
            dialog.status = series.status
            dialog.medium = series.medium
            dialog.ready = true
        }

        function onVolumesLoaded(seriesId, volumes) {
            if (seriesId === dialog.seriesId)
                dialog.volumes = volumes
        }

        function onVocabularyLoaded(vocabulary) {
            dialog.vocabulary = vocabulary
        }
    }

    Connections {
        target: Commands

        function onEdited(seriesId, entryId) {
            if (seriesId === dialog.seriesId && entryId.length === 0)
                dialog.close()
        }
    }

    Text {
        width: parent.width
        textFormat: Text.PlainText
        text: dialog.words.series
        color: Theme.ink
        font.family: Theme.displayFamily
        font.pixelSize: Widths.scale.title(20)
        font.weight: Font.Bold
        wrapMode: Text.WordWrap
    }

    // The cover on the line of the title, its picture at the left as in a volume's window: it is
    // the first thing the series is recognised by, and at the foot of a window that scrolls it
    // was the last thing anyone reached.
    Row {
        width: parent.width
        spacing: Widths.space.compact + Widths.space.compact

        CoverChoice {
            id: covering

            objectName: "series-edit-cover"
            prefix: "series-edit-cover"
            width: (parent.width - parent.spacing) * 0.45
            words: dialog.words
            current: dialog.loaded.cover ?? ""
            kept: (dialog.loaded.coverImage ?? false) || dialog.loaded.coverVolume !== undefined
            keptVolume: dialog.loaded.coverVolume
            volumes: dialog.volumes
            revertLabel: dialog.words.backToFirst
            defaultStatus: dialog.words.coverFirst
        }
        EditField {
            id: titleLine

            objectName: "series-edit-title"
            width: parent.width - covering.width - parent.spacing
            name: dialog.words.title
        }
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
            height: Widths.scale.size(110)
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

                    objectName: "series-edit-summary"
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

        Text {
            width: parent.width
            textFormat: Text.PlainText
            text: dialog.loaded.ownSummary ? dialog.words.ownSummary : dialog.words.workSummary
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            wrapMode: Text.WordWrap
        }
    }

    TokenField {
        id: authorsLine

        objectName: "series-edit-authors"
        name: dialog.words.authors
        hint: dialog.words.list
        createLabel: dialog.words.create
        vocabulary: dialog.vocabulary.authors ?? []
        multiple: true
        note: dialog.loaded.authorsDefault ?? ""
    }
    TokenField {
        id: artistsLine

        objectName: "series-edit-artists"
        name: dialog.words.artists
        hint: dialog.words.list
        createLabel: dialog.words.create
        vocabulary: dialog.vocabulary.artists ?? []
        multiple: true
        note: dialog.loaded.artistsDefault ?? ""
    }
    TokenField {
        id: genresLine

        objectName: "series-edit-genres"
        name: dialog.words.genres
        hint: dialog.words.list
        createLabel: dialog.words.create
        vocabulary: dialog.vocabulary.genres ?? []
        multiple: true
    }
    TokenField {
        id: tagsLine

        objectName: "series-edit-tags"
        name: dialog.words.tags
        hint: dialog.words.list
        createLabel: dialog.words.create
        vocabulary: dialog.vocabulary.tags ?? []
        multiple: true
    }
    Row {
        width: parent.width
        spacing: Widths.space.compact

        TokenField {
            id: publisherLine

            width: (parent.width - 2 * parent.spacing) / 3
            objectName: "series-edit-publisher"
            name: dialog.words.publisher
            hint: dialog.words.list
            createLabel: dialog.words.create
            vocabulary: dialog.vocabulary.publishers ?? []
            multiple: false
        }
        TokenField {
            id: collectionLine

            width: (parent.width - 2 * parent.spacing) / 3
            objectName: "series-edit-collection"
            name: dialog.words.collection
            hint: dialog.words.list
            createLabel: dialog.words.create
            vocabulary: dialog.vocabulary.collections ?? []
            multiple: false
        }
        TokenField {
            id: languageLine

            width: (parent.width - 2 * parent.spacing) / 3
            objectName: "series-edit-language"
            name: dialog.words.language
            hint: dialog.words.list
            createLabel: dialog.words.create
            vocabulary: dialog.vocabulary.languages ?? []
            worded: tag => Editing.languageName(tag)
            multiple: false
        }
    }

    Row {
        width: parent.width
        spacing: Widths.space.compact

        EditField {
            id: ageLine

            objectName: "series-edit-age"
            width: (parent.width - parent.spacing) / 2
            name: dialog.words.ageRating
        }
        EditField {
            id: countLine

            objectName: "series-edit-count"
            width: (parent.width - parent.spacing) / 2
            name: dialog.words.volumeCount
            inputMethodHints: Qt.ImhDigitsOnly
        }
    }

    Row {
        width: parent.width
        spacing: Widths.space.compact

        LeafSelect {
            objectName: "series-edit-status"
            width: (parent.width - parent.spacing) / 2
            name: dialog.words.status
            chosen: dialog.status
            options: [{ "value": -1, "label": dialog.words.unsaid },
                      { "value": 0, "label": dialog.words.ongoing },
                      { "value": 1, "label": dialog.words.completed },
                      { "value": 2, "label": dialog.words.hiatus },
                      { "value": 3, "label": dialog.words.cancelled }]
            onPicked: value => dialog.status = value
        }
        LeafSelect {
            objectName: "series-edit-medium"
            width: (parent.width - parent.spacing) / 2
            name: dialog.words.medium
            chosen: dialog.medium
            options: Editing.mediums
            onPicked: value => dialog.medium = value
        }
    }

    commands: [
        LeafTextAction {
            objectName: "series-edit-cancel"
            scaled: true
            label: dialog.words.cancel
            activeFocusOnTab: true
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: dialog.close()
        },

        ActionButton {
            objectName: "series-edit-save"
            scaled: true
            label: dialog.words.save
            ready: dialog.ready && !Commands.busy
            onTriggered: dialog.submit()
        }
    ]
}
