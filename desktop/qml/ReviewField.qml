// One line of a sheet: the field's name, its value as a box to type in or a choice to open,
// and where the value came from.
//
// A required field wears a green asterisk after its name, filled or not: told apart only by
// the red it wore when empty, a filled one looked as optional as a summary. A required value
// nobody said is not a blank box: it is edged in red and « Manquant » stands at its right. A
// value the sheet takes from above — a volume's drawer from its series, an edition's publisher
// from its work — is drawn in grey and edged in dashes, and is not missing. What is typed reaches the card when the box is left or Enter pressed, and only
// if it differs: every Tab through a sheet would otherwise be a correction of nothing, and a
// card corrected is a card re-announced at « Importer ».
//
// A list — authors, drawing, genres, tags — is words and not a line of text: each one a pill
// with its ×, the words of the same field elsewhere in the drop offered as one types, Enter
// adding one and Backspace taking the last one back. Typed as text with commas, « Dupont,
// Jean » was two people; each change reaches the card at once (`Review.edits.editWords`).
//
// Read-only when the sheet says so — a card already on its way, a file nobody could open: the
// value stays readable and the box takes no keystroke and no Tab, since a stop that cannot be
// changed is a stop for nothing.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Leaf

Item {
    id: line

    /// `{key, label, text, lacking, required, inherited, inheritedText, inheritedWords, hint,
    /// placeholder, choices, editable, doubt}`.
    required property var field
    /// The sheet's id, which `Review.edits.edit` takes the correction for.
    required property string sheetId
    /// `Review.sheet`, for the chapters' button, whose window writes the volume's list.
    property var sheet: ({})

    readonly property bool choosing: line.field.choices !== undefined
                                     && line.field.choices.length > 0
    readonly property bool editable: line.field.editable === true
    readonly property bool wordy: line.field.words !== undefined
    /// What the value is, and so what draws it: `line`, `long`, `date`, `number`, `chapters`.
    readonly property string kind: line.field.kind ?? "line"
    /// The first row of a list of words, framed: a row of pills or the box to add one, each a
    /// box round a word (`fit`) inside the frame's two pixels of padding (`size`). What the name
    /// is centred on, not the top of the row.
    /// The height of a one-line box of the sheet: a field's own, or a word's with room round it.
    readonly property real rowHeight: Math.max(Widths.scale.fit(32),
                                               Widths.scale.body(13) + Widths.scale.fit(18))
    readonly property real firstRow: Widths.scale.fit(28) + 2 * Widths.scale.size(2)
    readonly property bool plain: !line.choosing && !line.wordy
                                  && (line.kind === "line" || line.kind === "number")
    readonly property bool lacking: line.field.lacking === true
    readonly property bool inherited: line.field.inherited === true

    /// One of the two arrows beside a number: a step up or down by one.
    component Step: Item {
        id: step

        property string glyph: ""
        signal stepped()

        width: Widths.scale.size(22)
        height: Widths.scale.size(15)

        Glyph {
            anchors.centerIn: parent
            source: "assets/icons/" + step.glyph + ".svg"
            side: Widths.scale.size(16)
            tint: hoverStep.hovered ? Theme.ink : Theme.inkFaint
        }

        HoverHandler {
            id: hoverStep

            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: step.stepped()
        }
    }

    /// The number of the box moved by `by`, written the way a number is: a half stays a half.
    function stepNumber(by: real): void {
        const now = Number(String(input.text).replace(",", ".").trim())
        const next = (Number.isFinite(now) ? now : 0) + by
        if (next < 0)
            return
        Review.edits.edit(line.sheetId, line.field.key, String(next))
    }

    implicitHeight: box.height + 2 * Widths.scale.fit(7)
                    + (doubt.visible ? doubt.implicitHeight + Widths.space.tight : 0)
                    + (note.visible ? note.implicitHeight + Widths.space.tight : 0)

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.rule
    }

    /// The longest word of the label, which is what the column has to hold: the rest wraps.
    readonly property string longestWord: line.field.label.split(/\s+/).reduce(
        (longest, word) => word.length > longest.length ? word : longest, "")

    TextMetrics {
        id: ruler

        font: label.font
        text: line.longestWord + (line.field.required === true ? "\u00A0*" : "")
    }

    FontMetrics {
        id: oneLine

        font: label.font
    }

    Text {
        id: label

        objectName: "review-label-" + line.field.key
        anchors.left: parent.left
        // On the first row of its field and not centred on a tall one: against a summary or a
        // list of chapters a centred name floated half way down a box it names the top of.
        anchors.top: box.top
        // Centred on one line of the label, the first, whatever it comes to: a name that wraps
        // to two lines ( « Nom des chapitres » ) rose half a line above the row it names.
        anchors.topMargin: Math.round(((line.wordy ? line.firstRow : Widths.scale.fit(32))
                                       - oneLine.height) / 2)
        // The column's width: wide enough for the longest word of the name and no more, so that
        // it is the same on every row unless a word is longer than the column. The whole name
        // decided it, and « Nom des chapitres » pushed its field 40 pixels right of every other,
        // which are aligned on purpose; a name longer than the column wraps. The word is measured
        // and not a flat share of a narrow sheet, which cut « Scénario » and « Dessin » at the
        // fractional scale, where the word is a pixel or two wider than the width it was drawn for.
        width: Math.min(Math.max(Widths.scale.column(68), Math.ceil(ruler.advanceWidth) + 2),
                        Math.round(line.width * 0.42))
        // The asterisk of a required value is the last character of the name, so that it stays
        // glued to the word it marks when the name wraps: laid by itself at the width of the
        // whole name, « Sens de lecture » put it at the far edge of the column, a word away.
        text: line.field.label.replace(/&/g, "&amp;").replace(/</g, "&lt;")
              + (line.field.required === true
                 ? "\u00A0<b><font color=\"" + Theme.emerald + "\">*</font></b>" : "")
        textFormat: Text.RichText
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    Item {
        id: box

        anchors.left: label.right
        anchors.leftMargin: Widths.space.row
        anchors.right: parent.right
        anchors.rightMargin: lack.visible ? lack.implicitWidth + Widths.space.row : 0
        anchors.top: parent.top
        anchors.topMargin: Widths.scale.fit(7)
        height: line.wordy ? tokens.implicitHeight
              : line.kind === "long" ? Widths.scale.fit(132)
              : line.rowHeight

        TokenField {
            id: tokens

            objectName: line.wordy ? "review-words-" + line.field.key : ""
            width: parent.width
            visible: line.wordy
            name: ""
            spokenName: line.field.label
            readOnly: !line.editable
            multiple: true
            hint: line.wordy ? ReviewCaptions.addWordLabel(line.field.key) : ""
            createLabel: Editing.words.create
            vocabulary: line.field.vocabulary ?? []
            removeLabel: word => ReviewCaptions.removeWordLabel(line.field.label, word)
            framed: true
            alarmed: line.lacking
            inherited: line.field.inheritedWords ?? []
            note: line.field.hint ?? ""

            // The words of the card, even after a pill this field took itself: the sheet is
            // drawn again from the card on every change, and that is what it holds.
            Binding on words {
                value: line.field.words ?? []
                restoreMode: Binding.RestoreNone
            }

            onPicked: words => {
                // Refused — the same words, a card gone — the pills are the card's again.
                if (!Review.edits.editWords(line.sheetId, line.field.key, words))
                    tokens.words = line.field.words ?? []
            }
        }

        RegularExpressionValidator {
            id: numeric

            // A comma is how a French reader writes a half (`ReviewEdits::edit` reads both).
            regularExpression: /[0-9]*[.,]?[0-9]*/
        }

        LeafField {
            id: input

            // Named only when it is the one drawn: two items under one name, and a search
            // finds the hidden one.
            objectName: line.plain ? "review-field-" + line.field.key : ""
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            // A number is a few figures: a box as wide as a title said it had more to say.
            anchors.right: line.kind === "number" ? undefined : parent.right
            width: line.kind === "number" ? Math.min(parent.width, Widths.scale.column(96)) : 0
            visible: line.plain
            inputMethodHints: line.kind === "number" ? Qt.ImhFormattedNumbersOnly : Qt.ImhNone
            validator: line.kind === "number" ? numeric : null
            rightPadding: line.kind === "number" && line.editable ? Widths.scale.size(30) : 10
            Keys.onUpPressed: event => {
                if (line.kind === "number" && line.editable) {
                    line.stepNumber(1)
                    event.accepted = true
                }
            }
            Keys.onDownPressed: event => {
                if (line.kind === "number" && line.editable) {
                    line.stepNumber(-1)
                    event.accepted = true
                }
            }
            // Not a stop when read-only, on purpose (see the top of this file): a stop that
            // cannot be changed is a stop for nothing.
            activeFocusOnTab: visible && line.editable
            readOnly: !line.editable
            placeholderText: line.field.placeholder
            font.pixelSize: Widths.scale.body(13)
            selectByMouse: true

            // Edged in the rule at rest, as the frame was drawn. `LeafField` has no edge until
            // it holds the keys, which on the light theme's paper left a sheet of values with
            // no box round any of them: `onBar` is two shades off `paper` there.
            //
            // And no fill when nothing can be typed into it: a value read-only drawn as the box
            // one types in is a box clicked for nothing.
            background: Rectangle {
                radius: Widths.scale.size(6)
                color: line.editable && !line.inherited ? Theme.onBar : "transparent"
                border.color: input.activeFocus ? Theme.emerald
                              : line.inherited ? "transparent"
                              : line.lacking ? Theme.alert : Theme.rule
                border.width: 1

                DashedEdge {
                    anchors.fill: parent
                    visible: line.inherited && !input.activeFocus
                    radius: parent.radius
                    ink: Theme.inkFaint
                    fill: "transparent"
                }
            }
            color: line.inherited && !input.activeFocus ? Theme.inkFaint : Theme.ink

            // Shown from its start: a value longer than its box was shown by its end, the
            // cursor left there by the value being set — « subasa Yamaguchi ».
            onTextChanged: {
                if (!input.activeFocus)
                    input.cursorPosition = 0
            }

            // The value, except while it is being typed: the sheet is rebuilt on every change
            // of the queue — the retry countdown each second, another folder read, a chunk
            // landing — and a plain binding put the value back over what was half typed.
            // Left as it is when the keys go, not restored: `editingFinished` has read it.
            Binding on text {
                value: line.field.text
                when: !input.activeFocus
                restoreMode: Binding.RestoreNone
            }

            Accessible.name: line.field.label

            onEditingFinished: {
                if (line.editable && input.text !== line.field.text)
                    Review.edits.edit(line.sheetId, line.field.key, input.text)
            }
        }

        // The arrows of a number, on the right of its box: up and down by one, as the keys do.
        Column {
            objectName: line.kind === "number" ? "review-steps-" + line.field.key : ""
            anchors.verticalCenter: input.verticalCenter
            x: input.x + input.width - width - Widths.scale.size(4)
            visible: line.kind === "number" && line.editable

            Step {
                glyph: "expand_less"
                onStepped: line.stepNumber(1)
            }

            Step {
                glyph: "expand_more"
                onStepped: line.stepNumber(-1)
            }
        }

        // A summary is a paragraph: the line it was drawn as showed its first sentence and
        // hid the rest.
        Rectangle {
            id: paragraph

            objectName: line.kind === "long" ? "review-field-" + line.field.key : ""
            anchors.fill: parent
            visible: line.kind === "long"
            radius: Widths.scale.size(6)
            color: line.editable ? Theme.onBar : "transparent"
            border.color: area.activeFocus ? Theme.emerald : Theme.rule
            border.width: 1

            Flickable {
                anchors.fill: parent
                anchors.margins: Widths.scale.size(6)
                clip: true
                contentHeight: area.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                TextArea.flickable: TextArea {
                    id: area

                    activeFocusOnTab: paragraph.visible && line.editable
                    readOnly: !line.editable
                    wrapMode: TextEdit.Wrap
                    color: Theme.ink
                    selectionColor: Theme.emerald
                    selectedTextColor: Theme.onEmerald
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(13)
                    placeholderText: line.field.placeholder
                    placeholderTextColor: Theme.inkFaint
                    background: null
                    selectByMouse: true
                    Accessible.name: line.field.label

                    leftPadding: Widths.scale.size(6)
                    rightPadding: Widths.scale.size(6)
                    topPadding: Widths.scale.size(4)
                    bottomPadding: Widths.scale.size(4)

                    Binding on text {
                        value: line.field.text
                        when: !area.activeFocus
                        restoreMode: Binding.RestoreNone
                    }

                    // Shown from its first line: the cursor left where the text ended it
                    // opened on the last lines of a summary, cut at the top.
                    onTextChanged: {
                        if (!area.activeFocus)
                            area.cursorPosition = 0
                    }

                    onEditingFinished: {
                        if (line.editable && area.text !== line.field.text)
                            Review.edits.edit(line.sheetId, line.field.key, area.text)
                    }
                }
            }
        }

        // A date is a date: typed as JJ/MM/AAAA or chosen in the calendar, and a day that does
        // not exist is said before it is sent. The same field as the volume's own edit window.
        DateField {
            id: when

            objectName: line.kind === "date" ? "review-field-" + line.field.key : ""
            width: parent.width
            visible: line.kind === "date"
            name: ""
            words: Editing.words
            readOnly: !line.editable
            // As the boxes of the other rows of the sheet are drawn: edged in the rule, at the
            // sheet's body size, and as tall as the row's own box. It had none of the three, and
            // stood a pixel above and below its neighbours with a bare fill and a larger word.
            edged: true
            fontSize: Widths.scale.body(13)
            fieldHeight: line.rowHeight

            // The value, except while it is being typed: the sheet is drawn again on every
            // change of the queue.
            readonly property string shown: line.field.text
            onShownChanged: if (!when.typing) when.load(when.shown)
            Component.onCompleted: when.load(when.shown)

            onCommitted: {
                if (when.problem.length === 0 && when.value !== when.initial) {
                    Review.edits.edit(line.sheetId, line.field.key, when.value)
                    when.initial = when.value
                }
            }
        }

        // The chapters are a button on their own line, as the drawing has them: listed here,
        // bounded beside their name, a volume showed eight with their titles cut and the
        // button sat under the list, out of sight. The window is where they are read. Made
        // for that line alone: every field of a sheet carried a hidden button and its window.
        Loader {
            id: chapters

            width: parent.width
            anchors.verticalCenter: parent.verticalCenter
            active: line.kind === "chapters"
            visible: active

            // `setSource` and a `Binding`, not `sourceComponent: ReviewChapters { sheet: line.sheet }`:
            // Qt 6.4 will not instantiate a component bound to `line` once the context that made
            // it is left (« Cannot instantiate bound component outside its creation context »),
            // so the button was never drawn there, where 6.10 drew it.
            // `sheet` is required by the component, so it is handed over at creation.
            function open() {
                if (active && !item)
                    setSource("ReviewChapters.qml", { sheet: line.sheet })
            }
            onActiveChanged: open()
            Component.onCompleted: open()

            Binding {
                target: chapters.item
                property: "sheet"
                value: line.sheet
                when: chapters.item !== null
            }
        }

        // A list nobody can add to and that holds nothing: its pills were the only thing that
        // said so, and read-only they are not drawn.
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            visible: line.wordy && !line.editable && (line.field.words ?? []).length === 0
            textFormat: Text.PlainText
            text: line.field.placeholder
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
        }

        // A choice is one of its values and nothing typed: « droite » written into a
        // `work.json` is a direction the server does not read. So a box that opens a menu,
        // shaped as the field beside it.
        Rectangle {
            id: chooser

            objectName: line.choosing ? "review-field-" + line.field.key : ""
            anchors.fill: parent
            visible: line.choosing
            radius: Widths.scale.size(6)
            color: line.editable && !line.inherited ? Theme.onBar : "transparent"
            border.color: chooser.activeFocus ? Theme.emerald
                          : line.inherited ? "transparent"
                          : line.lacking ? Theme.alert : Theme.rule
            border.width: 1
            activeFocusOnTab: visible && line.editable

            DashedEdge {
                anchors.fill: parent
                visible: line.inherited && !chooser.activeFocus
                radius: chooser.radius
                ink: Theme.inkFaint
                fill: "transparent"
            }

            Accessible.role: Accessible.ComboBox
            Accessible.name: line.field.label
            Accessible.description: chosen.text

            Keys.onPressed: event => {
                if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                        || event.key === Qt.Key_Enter
                        || (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier))) {
                    choices.open()
                    event.accepted = true
                }
            }

            HoverHandler {
                enabled: line.editable
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                enabled: line.editable
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: {
                    chooser.forceActiveFocus(Qt.MouseFocusReason)
                    choices.open()
                }
            }

            Text {
                id: chosen

                anchors.left: parent.left
                anchors.leftMargin: Widths.scale.size(10)
                anchors.right: arrow.left
                anchors.rightMargin: Widths.space.label
                anchors.verticalCenter: parent.verticalCenter
                readonly property bool empty: line.field.text.length === 0
                // Read from the field itself and not through `empty`: a box drawn before its
                // sheet arrived kept « Non renseigné » for a value the sheet held, the word
                // having been evaluated against the stale answer of the property beside it.
                text: line.field.text.length === 0 ? line.field.placeholder : line.field.text
                textFormat: Text.PlainText
                color: line.field.text.length === 0 || line.inherited ? Theme.inkFaint : Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.body(13)
                elide: Text.ElideRight
            }

            FoldChevron {
                id: arrow

                anchors.right: parent.right
                anchors.rightMargin: Widths.scale.size(10)
                anchors.verticalCenter: parent.verticalCenter
                visible: line.editable
                open: true
                tint: Theme.inkFaint
            }

            LeafMenu {
                id: choices

                objectName: "review-choices-" + line.field.key
                y: chooser.height + Widths.space.compact

                Repeater {
                    model: line.choosing ? line.field.choices : []

                    delegate: LeafMenuItem {
                        required property var modelData

                        objectName: "review-choice-" + line.field.key + "-" + modelData.value
                        text: modelData.label
                        checkable: true
                        checked: line.field.text === modelData.label
                        // The one already chosen is no correction, as in the box beside it.
                        onTriggered: {
                            if (line.field.text !== modelData.label)
                                Review.edits.edit(line.sheetId, line.field.key, modelData.value)
                        }
                    }
                }
            }
        }
    }

    // A required value nobody gave: its word at the right of the box, on its first row.
    Text {
        id: lack

        objectName: "review-lack-" + line.field.key
        visible: line.lacking
        anchors.right: parent.right
        anchors.verticalCenter: label.verticalCenter
        text: ReviewCaptions.missingLabel
        textFormat: Text.PlainText
        color: Theme.alert
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        font.weight: Font.DemiBold

        Accessible.role: Accessible.StaticText
        Accessible.name: ReviewCaptions.missingLabel
    }

    // Under a box of one line, in grey, what its value means — how a chapter label is
    // written — as a list of words says it under its own pills (`TokenField.note`).
    Text {
        id: note

        objectName: "review-note-" + line.field.key
        anchors.left: box.left
        anchors.right: parent.right
        anchors.top: doubt.visible ? doubt.bottom : box.bottom
        anchors.topMargin: Widths.space.tight
        visible: !line.wordy && text.length > 0
        text: line.field.hint ?? ""
        textFormat: Text.PlainText
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        wrapMode: Text.WordWrap
    }

    // Under the box, as it became wrong: what was typed and why it was not taken.
    Text {
        id: doubt

        objectName: "review-doubt-" + line.field.key
        anchors.left: box.left
        anchors.right: parent.right
        anchors.top: box.bottom
        anchors.topMargin: Widths.space.tight
        visible: text.length > 0
        text: line.field.doubt === undefined ? "" : line.field.doubt
        textFormat: Text.PlainText
        color: Theme.alert
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        wrapMode: Text.WordWrap

        Accessible.role: Accessible.AlertMessage
        Accessible.name: text
    }
}
