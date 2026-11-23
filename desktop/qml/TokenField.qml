// A list of words typed with the library's own vocabulary under the fingers.
//
// What is already used is suggested as one types, and a word that is not is created with an
// explicit « Créer « horeur » » row — after the near ones, so that a name one letter away from
// "Horreur" is offered as that before it is offered as a new word. A free text field let
// "Horreur" and "Horeur" be two genres, and nothing said so until the filters listed both.
//
// Offered, never imposed: Enter keeps what was typed, and a suggestion is taken only when it
// was chosen — with ↓ and ↑, or the pointer. Enter took the first row, and the first row is
// whatever comes first: « Auteur M » became « Auteur B », one letter away, « Morvan » became
// « Jean-David Morvan », the more used name holding it, and either was written into the
// archive under a name nobody had typed. What is typed is still kept under the vocabulary's
// spelling when it is the same word, by case or by accents (`stored`).
//
// `multiple` keeps the picked words as chips and lets more be added; without it the field
// holds one word, shown in the field itself (a publisher, a language).
//
// `inherited` is what an empty list stands for elsewhere — the series' writer, for a volume
// that names none —, shown in grey, edged in dashes and with no ×: nothing of it is the field's
// to remove, and the first word typed replaces all of it.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Leaf

Column {
    id: field

    required property string name
    /// What the library already uses, the most used first.
    property var vocabulary: []
    property bool multiple: true
    /// The words held. For a single field, at most one.
    property var words: []
    property string hint: ""
    /// What the words stand for, said under the field: « Pour les albums ne précisant pas de
    /// scénariste ». Nothing when empty.
    property string note: ""
    /// The words an empty list draws on, shown in its place and never sent.
    property var inherited: []
    readonly property bool standing: field.multiple && field.words.length === 0
                                     && field.inherited.length > 0
    /// The pills and the box to type in drawn as one field, edged round both: on a sheet of
    /// fields each in its box, a list of names stood as pills over a box of its own.
    property bool framed: false
    /// Edged in the alert's ink, framed: a required list nobody filled.
    property bool alarmed: false
    readonly property bool showsWords: field.multiple && (field.words.length > 0 || field.standing)
    readonly property bool takesKeys: !field.readOnly && (field.multiple || field.words.length === 0)
    /// « Créer « %1 » » and « Ajouter », worded by the owner.
    property string createLabel: ""
    property alias input: input
    /// How a stored word is shown: « fr » held, « Français » read. Typing either finds it, and
    /// it is the stored word that is kept, so the field is the language's name to the eye and
    /// its tag to the library.
    property var worded: word => word
    /// Nothing typed and nothing removed: the words are read, not changed.
    property bool readOnly: false
    /// What the box is called aloud, its title by default. A field titled beside itself
    /// rather than above passes its label: a box with no name read « modifiable » alone.
    property string spokenName: field.name
    /// What the × of a word is called aloud: the word alone said nothing of the field it
    /// would leave, on a sheet of four lists of names.
    property var removeLabel: word => field.worded(word)

    width: parent.width
    spacing: Widths.space.compact

    /// Without accents, case or punctuation, as the server folds a name: an apostrophe joins and
    /// the ligatures are spelled out, so « D’Aviau » is « daviau » and « Cœur » is « coeur ».
    /// Folded otherwise, a word the library holds once was offered here as two, or a second
    /// spelling of it was taken as new.
    function folded(text) {
        return Editing.searchKey(text)
    }

    /// Letters apart, bounded: two words are « near » when a typo explains the gap.
    function apart(a, b) {
        if (Math.abs(a.length - b.length) > 2)
            return 3
        let before = Array.from({ length: b.length + 1 }, (_, i) => i)
        for (let i = 1; i <= a.length; ++i) {
            const now = [i]
            for (let j = 1; j <= b.length; ++j) {
                now[j] = Math.min(before[j] + 1, now[j - 1] + 1,
                                  before[j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1))
            }
            before = now
        }
        return before[b.length]
    }

    /// Whether a stored word is what `key` spells, by the word or by how it is shown.
    function matches(word, key) {
        return field.folded(word) === key || field.folded(field.worded(word)) === key
    }

    /// What is typed as the word the library holds: « Français » is « fr » when it is the one
    /// the vocabulary knows by that name.
    function stored(typed) {
        const key = field.folded(typed)
        return field.vocabulary.find(one => field.matches(one, key)) ?? typed
    }

    readonly property string typed: input.text.trim()
    /// The suggestion chosen with the arrows, -1 for what is typed. Typing again unchooses it:
    /// a row chosen for « Hor » is not the one meant once « Horeur » is typed.
    property int chosen: -1
    onTypedChanged: field.chosen = -1
    readonly property string key: field.folded(field.typed)
    readonly property bool exact: field.vocabulary.some(one => field.matches(one, field.key))
                                  || field.words.some(one => field.matches(one, field.key))

    /// `[{ word, create }]`: the words that contain what is typed, then the ones a typo away,
    /// then the creation of exactly what is typed when nothing is that word already.
    readonly property var suggestions: {
        if (field.key.length === 0)
            return []
        const taken = field.words.map(one => field.folded(one))
        const free = field.vocabulary.filter(one => !taken.includes(field.folded(one)))
        const seen = one => [field.folded(one), field.folded(field.worded(one))]
        const holding = free.filter(one => seen(one).some(text => text.includes(field.key)))
        const near = field.key.length < 4 ? [] : free.filter(one => {
            return !holding.includes(one)
                   && seen(one).some(text => field.apart(text, field.key) <= 2)
        })
        const all = holding.concat(near).slice(0, 6).map(one => ({
            "word": one, "shown": field.worded(one), "create": false }))
        if (!field.exact)
            all.push({ "word": field.typed, "shown": field.typed, "create": true })
        return all
    }

    signal picked(var words)

    /// What a suggestion row reads: the word, or the creation of what is typed.
    function shownOf(suggestion) {
        return suggestion.create ? field.createLabel.replace("%1", suggestion.word)
                                 : suggestion.shown
    }

    function take(word) {
        const clean = field.stored(word.trim())
        if (clean.length === 0)
            return
        // Once, as the server keeps a list: « arleston » beside « Arleston » is her again.
        const key = field.folded(clean)
        if (field.multiple && field.words.some(one => field.folded(one) === key)) {
            input.text = ""
            return
        }
        const next = field.multiple ? field.words.concat([clean]) : [clean]
        field.words = next
        input.text = ""
        field.picked(next)
    }

    function drop(index) {
        const next = field.words.filter((_, at) => at !== index)
        field.words = next
        field.picked(next)
    }

    /// The × of word `index` takes the keys; past the last, the box does.
    function reach(index) {
        const chip = chipRepeater.itemAt(index)
        if (index >= 0 && chip)
            chip.remover.forceActiveFocus(Qt.TabFocusReason)
        else if (index >= field.words.length && input.visible)
            input.forceActiveFocus(Qt.TabFocusReason)
    }

    Text {
        visible: field.name.length > 0
        textFormat: Text.PlainText
        text: field.name
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        font.weight: Font.Medium
    }

    Item {
        id: frame

        // As tall as a box beside it, a pill high and two pixels round: a row of names stood
        // ten pixels taller than the title above it.
        readonly property real pad: field.framed ? Widths.scale.size(2) : 0

        width: parent.width
        height: inner.implicitHeight + 2 * frame.pad
        visible: field.showsWords || field.takesKeys

        Rectangle {
            anchors.fill: parent
            visible: field.framed
            radius: Widths.scale.size(6)
            color: field.readOnly ? "transparent" : Theme.onBar
            border.width: 1
            border.color: input.activeFocus ? Theme.emerald
                          : field.alarmed ? Theme.alert : Theme.rule
        }

        Column {
            id: inner

            x: frame.pad
            y: frame.pad
            width: frame.width - 2 * frame.pad
            spacing: field.framed ? Widths.space.tight : field.spacing

            Flow {
                id: chips

                width: parent.width
                spacing: Widths.space.compact
                // A single word is shown once, in the field under it, not twice. Framed, it
                // holds the box to type in too, after the last name.
                visible: field.showsWords || (field.framed && field.takesKeys)

                Repeater {
                    id: chipRepeater

                    model: field.standing ? field.inherited : field.words

                    delegate: Rectangle {
                        id: chip

                        required property int index
                        required property string modelData
                        property alias remover: remove
                        readonly property string word: chip.modelData
                        readonly property bool inherited: field.standing

                        objectName: field.objectName + "-chip-" + chip.index
                        // Boxes round a word follow the word (`fit`), not the window.
                        height: Widths.scale.fit(28)
                        // Ten before the word, and after it either ten (no ×) or the × with its
                        // four on each side. Read only, a name has no ×: it was given 8 after
                        // it against 10 before, since the room was counted for a × that is gone.
                        width: wording.implicitWidth + (remove.visible ? remove.width : 0)
                               + Widths.scale.fit(remove.visible ? 18 : 20)
                        radius: height / 2
                        color: chip.inherited ? "transparent" : Theme.emeraldWash

                        DashedEdge {
                            anchors.fill: parent
                            visible: chip.inherited
                            ink: Theme.inkFaint
                            fill: "transparent"
                        }

                        // The word itself, said: read only, its × is gone, and it was the × alone
                        // that named it.
                        Accessible.role: Accessible.StaticText
                        Accessible.name: field.worded(chip.modelData)

                        Text {
                            id: wording

                            anchors.left: parent.left
                            anchors.leftMargin: Widths.scale.fit(10)
                            anchors.verticalCenter: parent.verticalCenter
                            text: field.worded(chip.modelData)
                            textFormat: Text.PlainText
                            color: chip.inherited ? Theme.inkFaint : Theme.emerald
                            font.family: Theme.textFamily
                            font.pixelSize: Widths.scale.body(13)
                        }

                        IconButton {
                            id: remove

                            objectName: chip.objectName + "-remove"
                            anchors.right: parent.right
                            anchors.rightMargin: Widths.scale.fit(4)
                            anchors.verticalCenter: parent.verticalCenter
                            width: Widths.scale.fit(20)
                            height: width
                            side: Widths.scale.icon(14)
                            glyph: "close"
                            label: field.removeLabel(chip.modelData)
                            visible: !field.readOnly && !chip.inherited
                            activeFocusOnTab: visible
                            // The arrows walk the words, as they walk the letters of the box after them.
                            Keys.onLeftPressed: field.reach(Math.max(0, chip.index - 1))
                            Keys.onRightPressed: field.reach(chip.index + 1)
                            // The keys go to the box: on a word removed, they were nowhere.
                            onTriggered: {
                                const keys = remove.activeFocus
                                field.drop(chip.index)
                                if (keys && input.visible)
                                    input.forceActiveFocus(Qt.TabFocusReason)
                            }
                        }
                    }
                }
            }

            LeafField {
                id: input

                objectName: field.objectName + "-field"
                bare: field.framed
                // Framed, on the line of the names when it fits there, taking what the last
                // name leaves of it: at a fixed width it went under « Akira Toriyama » with the
                // room for « Ajouter un nom » beside him. Out of room, under them, whole.
                parent: field.framed ? chips : inner
                readonly property Item last: chipRepeater.count > 0
                                             ? chipRepeater.itemAt(chipRepeater.count - 1) : null
                readonly property real room: input.last === null ? chips.width
                                             : chips.width - input.last.x - input.last.width
                                               - chips.spacing
                readonly property real least: wanted.advanceWidth + input.leftPadding
                                               + input.rightPadding
                width: !field.framed || !field.showsWords || input.room < input.least
                       ? parent.width : input.room
                height: field.framed ? Widths.scale.fit(28) : 34
                activeFocusOnTab: visible
                placeholderText: field.multiple || field.words.length === 0 ? field.hint : ""
                // One word, held: the field is that word, and typing replaces it.
                visible: field.takesKeys
                Accessible.name: field.spokenName

                // What the box says while empty, or a name's worth of room when it says nothing.
                TextMetrics {
                    id: wanted

                    font: input.font
                    text: input.placeholderText.length > 0 ? input.placeholderText : "Toriyama"
                }
                // The row chosen is said where the keys are: they stay in the box.
                Accessible.description: field.chosen >= 0 && field.suggestions[field.chosen]
                                        ? field.shownOf(field.suggestions[field.chosen]) : ""
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                            || event.text === ",") {
                        const chosen = field.suggestions[field.chosen]
                        field.take(chosen ? chosen.word : field.typed)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Down && field.suggestions.length > 0) {
                        field.chosen = Math.min(field.chosen + 1, field.suggestions.length - 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Up && field.chosen >= 0) {
                        field.chosen = field.chosen - 1
                        event.accepted = true
                    } else if (event.key === Qt.Key_Backspace && input.text.length === 0
                               && field.words.length > 0) {
                        field.drop(field.words.length - 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Left && input.cursorPosition === 0
                               && field.multiple && field.words.length > 0) {
                        field.reach(field.words.length - 1)
                        event.accepted = true
                    }
                }
            }
        }
    }

    // The single value, shown once held, with the way to change it.
    Rectangle {
        visible: !field.multiple && field.words.length > 0
        width: parent.width
        height: Widths.scale.size(34)
        radius: 6
        color: Theme.onBar

        Text {
            objectName: field.objectName + "-held"
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: field.worded(field.words[0] ?? "")
            textFormat: Text.PlainText
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: 14
        }

        IconButton {
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            width: Widths.scale.size(26)
            height: width
            side: Widths.scale.icon(16)
            glyph: "close"
            label: field.worded(field.words[0] ?? "")
            activeFocusOnTab: true
            onTriggered: field.drop(0)
        }
    }

    Column {
        objectName: field.objectName + "-suggestions"
        width: parent.width
        visible: field.suggestions.length > 0 && input.visible

        Repeater {
            model: field.suggestions

            delegate: Rectangle {
                id: row

                required property int index
                required property var modelData
                readonly property bool chosen: field.chosen === row.index

                objectName: field.objectName + "-suggestion-" + row.index
                width: parent.width
                height: Widths.scale.size(32)
                radius: 6
                color: pointer.hovered || row.chosen ? Theme.onPaper : "transparent"

                Accessible.role: Accessible.ListItem
                Accessible.name: field.shownOf(row.modelData)
                Accessible.selected: row.chosen
                Accessible.onPressAction: field.take(row.modelData.word)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: field.shownOf(row.modelData)
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: row.modelData.create ? Theme.emerald : Theme.ink
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(13)
                    font.weight: row.modelData.create ? Font.Medium : Font.Normal
                }

                HoverHandler { id: pointer; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: field.take(row.modelData.word)
                }
            }
        }
    }

    Text {
        objectName: field.objectName + "-note"
        visible: field.note.length > 0
        width: parent.width
        text: field.note
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
    }
}
