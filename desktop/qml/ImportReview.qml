// The checking frame: one, whatever was dropped — a series or fifty. Four zones always there:
// the filters over everything, the tree on the left, the list in the middle, the sheet on the
// right; and under them the footer that says what still stops the step and the way on.
//
// The three columns keep the proportions they were drawn with (210, the rest, 410) for a
// window of 1280, and give way to the list's share as the window narrows: at the smallest
// window and a text half as large again, the drawn widths came to more than the box itself,
// and the list between them went to nothing.
//
// Tab walks it in the order it is read — the search, the filters, the tree, « Revoir la
// suivante », « Tout », the list, the sheet's fields (or what the ticked series take), the way
// back and the way on — because that is the order its items are declared in. The tree and the
// list are one stop each, walked with the arrows: at fifty series, a stop per line was a
// hundred presses to cross the frame. Ctrl+F puts the keys in the search.
//
// An order of a universe opened from its sheet takes the same four zones (`OrderEditor`): its
// tabs where the filters were, the universe in the tree's place, the steps in the middle, the
// step selected on the right, and « Retour » and « Enregistrer » in the footer. Escape is
// « Retour » there, before it is the window's ×, and the keys go back to the line the order
// was opened from.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Item {
    id: frame

    /// Back to the cards, giving nothing up.
    signal back()
    /// « Suivant » pressed: what can go is ready, and nothing is sent yet.
    signal imported()
    /// Nothing to import: the window closes.
    signal done()

    /// Where the keys land when the frame opens: the first filter, and not the button that
    /// imports — an Enter pressed out of habit would send what nobody has looked at.
    readonly property Item firstStop: filters.count > 0 ? filters.itemAt(0) : null

    /// Read once, and only while the frame is shown, for the reason `ReviewList` gives.
    readonly property var counts: frame.visible ? Review.counts : []

    /// An order is being written.
    readonly property bool editing: frame.visible && OrderEditor.open
    /// The order opened, whose line on the sheet takes the keys back when the editor goes —
    /// by its id and not the line itself: the sheet is emptied while it is hidden, and the line
    /// the keys came from is not the one drawn when it comes back.
    property string opened: ""

    Connections {
        target: Editing

        function onVocabularyLoaded(vocabulary) {
            Review.edits.setLibraryWords(vocabulary)
        }
    }

    function openOrder(orderId: string): void {
        if (!OrderEditor.openOn(Review.sheet.id ?? "", orderId))
            return
        frame.opened = orderId
        Qt.callLater(frame.keysToTheTab)
    }

    function keysToTheTab(): void {
        for (let at = 0; at < tabs.count; ++at) {
            const tab = tabs.itemAt(at)
            if (tab && tab.chosen) {
                tab.forceActiveFocus(Qt.TabFocusReason)
                return
            }
        }
    }

    /// Back to the frame, the draft let go unless `save` took it, the keys where they were.
    function leaveEditor(): void {
        if (OrderEditor.open)
            OrderEditor.close()
        Qt.callLater(() => {
            if (!sheet.focusOrder(frame.opened))
                rail.takeKeys()
        })
    }

    // Shut with the frame: a draft left open would greet the next look at another drop.
    onVisibleChanged: {
        if (!visible && OrderEditor.open)
            OrderEditor.close()
        // The names the library already holds, offered by the sheet's list fields after the
        // import's: read again each time the frame opens, the last import having added some.
        if (visible)
            Editing.loadVocabulary()
    }

    // Taken before the window's ×: inside the editor, Escape means « Retour ».
    Keys.onShortcutOverride: event => event.accepted = frame.editing
                                                       && event.key === Qt.Key_Escape
    Keys.onEscapePressed: event => {
        if (frame.editing)
            frame.leaveEditor()
        else
            event.accepted = false
    }

    // 250 and not the frame's 210: at 210 the editions of a series in a universe, three
    // levels down, read « Édition ori… » and « Perfect Ed… ».
    readonly property int railWidth: Math.min(Widths.scale.column(250), Math.round(frame.width * 0.28))
    readonly property int sheetWidth: Math.min(Widths.scale.column(410), Math.round(frame.width * 0.4))

    objectName: "import-review"

    // ——— The filters ————————————————————————————————————————————————————————————————

    Item {
        id: toolbar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.max(Widths.scale.size(54), pills.height + 2 * Widths.space.row,
                         orderTabs.height + 2 * Widths.space.row)

        Item {
            objectName: "review-filters"
            anchors.fill: parent
            visible: !frame.editing

            // What is typed narrows the list and its counters, not the tree: the tree says
            // where things are, and a search that emptied it would leave nothing to stand on.
            LeafField {
                id: search

                objectName: "review-search"
                anchors.left: parent.left
                anchors.leftMargin: Widths.space.card
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(Widths.scale.column(240), Math.round(frame.width * 0.22))
                height: Math.max(Widths.scale.fit(30), contentHeight + Widths.scale.fit(8))
                edged: true
                activeFocusOnTab: true
                font.pixelSize: Widths.scale.body(13)
                placeholderText: ReviewCaptions.searchLabel
                onTextEdited: Review.view.search = search.text

                Accessible.name: ReviewCaptions.searchLabel

                // Emptied by the frame itself when it opens on what is to review.
                Connections {
                    target: Review

                    function onChanged() {
                        if (search.text !== Review.view.search)
                            search.text = Review.view.search
                    }
                }
            }

            Shortcut {
                sequences: [StandardKey.Find]
                enabled: frame.visible && !frame.editing
                onActivated: search.forceActiveFocus(Qt.ShortcutFocusReason)
            }

            Row {
                id: pills

                anchors.left: search.right
                anchors.leftMargin: Widths.space.row
                anchors.verticalCenter: parent.verticalCenter
                spacing: Widths.scale.fit(4)

                Repeater {
                    id: filters

                    model: frame.counts.length

                    delegate: Pill {
                        id: pill

                        required property int index
                        readonly property var count: frame.counts[index] ?? ({ label: "", count: 0,
                                                                               lit: false })

                        objectName: "review-filter-" + index
                        label: pill.count.label
                        tally: pill.count.count
                        lit: pill.count.lit === true
                        textSize: Widths.scale.body(13)
                        padding: Widths.scale.fit(12)
                        height: Math.max(Widths.scale.fit(30), wordHeight + Widths.scale.fit(8))
                        activeFocusOnTab: true
                        onPressed: Review.view.setFilter(pill.index)

                        Accessible.role: Accessible.Button
                        Accessible.name: pill.label
                        Accessible.checkable: true
                        Accessible.checked: pill.lit
                        Accessible.onPressAction: Review.view.setFilter(pill.index)

                        Keys.onPressed: event => {
                            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                                    || event.key === Qt.Key_Enter) {
                                Review.view.setFilter(pill.index)
                                event.accepted = true
                            }
                        }

                        FocusRing {
                            objectName: pill.objectName + "-focus"
                            cornerRadius: pill.height / 2
                        }
                    }
                }
            }

            Text {
                objectName: "review-note"
                anchors.left: pills.right
                anchors.leftMargin: Widths.space.row
                anchors.right: legend.left
                anchors.rightMargin: Widths.space.row
                anchors.verticalCenter: parent.verticalCenter
                // What the tree said under itself before it said how a role is changed: once a
                // role is proposed, the box under it holds that, and « Rien n'est dit » or the
                // rule of a universe's root comes here, over the whole drop it is about.
                text: !frame.visible ? ""
                    : Review.note.length > 0 || Review.railHint.length === 0 ? Review.note
                    : Review.railNote
                textFormat: Text.PlainText
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideLeft
            }

            // What the green asterisk after a field's name means, said once for every sheet.
            Text {
                id: legend

                objectName: "review-required-legend"
                anchors.right: parent.right
                anchors.rightMargin: Widths.space.card
                anchors.verticalCenter: parent.verticalCenter
                text: "<b><font color=\"" + Theme.emerald + "\">*</font></b> "
                      + ReviewCaptions.requiredLegend
                textFormat: Text.StyledText
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)

                Accessible.name: ReviewCaptions.requiredLegend
            }
        }

        // The orders of the universe, one tab each, while one of them is written.
        Row {
            id: orderTabs

            anchors.left: parent.left
            anchors.leftMargin: Widths.space.card
            anchors.verticalCenter: parent.verticalCenter
            visible: frame.editing
            spacing: Widths.scale.fit(4)

            Repeater {
                id: tabs

                model: frame.editing ? OrderEditor.tabs.length : 0

                delegate: Pill {
                    id: tab

                    required property int index
                    readonly property var said: OrderEditor.tabs[index]
                                                ?? ({ id: "", name: "", works: 0, chosen: false })
                    readonly property bool chosen: tab.said.chosen === true

                    objectName: "order-tab-" + tab.said.id
                    label: tab.said.name
                    tally: tab.said.works
                    lit: tab.chosen
                    textSize: Widths.scale.body(13)
                    padding: Widths.scale.fit(12)
                    height: Math.max(Widths.scale.fit(30), wordHeight + Widths.scale.fit(8))
                    activeFocusOnTab: true
                    onPressed: OrderEditor.choose(tab.said.id)

                    Accessible.role: Accessible.PageTab
                    Accessible.name: tab.label
                    Accessible.selected: tab.chosen
                    Accessible.onPressAction: OrderEditor.choose(tab.said.id)

                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                                || event.key === Qt.Key_Enter) {
                            OrderEditor.choose(tab.said.id)
                            event.accepted = true
                        }
                    }

                    FocusRing {
                        objectName: tab.objectName + "-focus"
                        cornerRadius: tab.height / 2
                    }
                }
            }
        }

        Text {
            objectName: "order-drag-hint"
            anchors.left: orderTabs.right
            anchors.leftMargin: Widths.space.row
            anchors.right: parent.right
            anchors.rightMargin: Widths.space.card
            anchors.verticalCenter: parent.verticalCenter
            visible: frame.editing
            text: SheetCaptions.orderWords.dragHint
            textFormat: Text.PlainText
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.rule
        }
    }

    // ——— The names an import asks for ————————————————————————————————————————————————————

    // Adding editions to a series: each edition the server cannot name is asked one here, and
    // the series' own current edition too when it never had a name. Nothing is drawn, and no
    // height taken, for any other import.
    Column {
        id: names

        objectName: "review-edition-names"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: toolbar.bottom
        visible: Review.editionRows.length > 0 && !frame.editing
        height: visible ? implicitHeight : 0
        spacing: Widths.space.compact

        Repeater {
            // The count, and not the rows: the rows are made again at every answer, and a
            // field rebuilt under the hand that types in it loses what was typed.
            model: Review.editionRows.length

            Item {
                id: line

                required property int index
                readonly property var row: Review.editionRows[index] || ({})

                objectName: "review-edition-name-" + index
                width: names.width
                height: Math.max(Widths.scale.fit(34), nameField.height) + Widths.space.row

                Text {
                    id: lineLabel

                    anchors.left: parent.left
                    anchors.leftMargin: Widths.space.card
                    anchors.verticalCenter: nameField.verticalCenter
                    width: Math.min(Widths.scale.column(180), Math.round(frame.width * 0.2))
                    text: (line.row.label || "") + " *"
                    textFormat: Text.PlainText
                    color: Theme.ink
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(13)
                    elide: Text.ElideRight
                }

                LeafField {
                    id: nameField

                    objectName: "review-edition-name-field-" + line.index
                    anchors.left: lineLabel.right
                    anchors.leftMargin: Widths.space.row
                    anchors.top: parent.top
                    anchors.topMargin: Widths.space.compact
                    width: Math.min(Widths.scale.column(260), Math.round(frame.width * 0.3))
                    height: Math.max(Widths.scale.fit(30), contentHeight + Widths.scale.fit(8))
                    edged: true
                    activeFocusOnTab: true
                    font.pixelSize: Widths.scale.body(13)
                    Component.onCompleted: text = line.row.name || ""
                    onEditingFinished: Review.edits.setEditionName(line.row.path, text)

                    // A field being typed in is the reader's; any other follows the card.
                    Connections {
                        target: Review

                        function onChanged() {
                            const said = line.row.name || ""
                            if (!nameField.activeFocus && nameField.text !== said)
                                nameField.text = said
                        }
                    }

                    Accessible.name: lineLabel.text
                }

                Text {
                    objectName: "review-edition-name-help-" + line.index
                    anchors.left: nameField.right
                    anchors.leftMargin: Widths.space.row
                    anchors.right: parent.right
                    anchors.rightMargin: Widths.space.card
                    anchors.verticalCenter: nameField.verticalCenter
                    text: (line.row.problem || "").length > 0 ? line.row.problem
                                                           : Review.editionNameHelp
                    textFormat: Text.PlainText
                    color: (line.row.problem || "").length > 0 ? Theme.alert : Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                    elide: Text.ElideRight
                }
            }
        }
    }

    // ——— The three columns ——————————————————————————————————————————————————————————

    ReviewRail {
        id: rail

        objectName: "review-rail"
        x: 0
        anchors.top: names.bottom
        anchors.bottom: footer.top
        width: frame.railWidth
    }

    Rectangle {
        anchors.left: rail.right
        anchors.top: names.bottom
        anchors.bottom: footer.top
        width: 1
        color: Theme.rule
    }

    ReviewList {
        objectName: "review-list"
        visible: !frame.editing
        anchors.left: rail.right
        anchors.leftMargin: 1
        anchors.right: sheet.left
        anchors.rightMargin: 1
        anchors.top: names.bottom
        anchors.bottom: footer.top
    }

    Rectangle {
        anchors.right: sheet.left
        anchors.top: names.bottom
        anchors.bottom: footer.top
        width: 1
        color: Theme.rule
    }

    OrderSteps {
        objectName: "order-editor"
        visible: frame.editing
        anchors.left: rail.right
        anchors.leftMargin: 1
        anchors.right: sheet.left
        anchors.rightMargin: 1
        anchors.top: names.bottom
        anchors.bottom: footer.top
    }

    OrderStepSheet {
        objectName: "order-sheet"
        visible: frame.editing
        anchors.right: parent.right
        anchors.top: names.bottom
        anchors.bottom: footer.top
        width: frame.sheetWidth
    }

    ReviewSheet {
        id: sheet

        objectName: "review-sheet"
        visible: !frame.editing && !Review.ticks.groupedShown
        onKeysLost: rail.takeKeys()
        onOrderOpening: orderId => frame.openOrder(orderId)
        anchors.right: parent.right
        anchors.top: names.bottom
        anchors.bottom: footer.top
        width: frame.sheetWidth
    }

    // In the sheet's place while the last gesture is a tick.
    ReviewGrouped {
        objectName: "review-grouped"
        visible: !frame.editing && Review.ticks.groupedShown
        anchors.right: parent.right
        anchors.top: names.bottom
        anchors.bottom: footer.top
        width: frame.sheetWidth
    }

    // ——— The way on —————————————————————————————————————————————————————————————————

    Item {
        id: footer

        objectName: "review-footer"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.max(Widths.scale.size(64),
                         Math.max(ways.height, said.implicitHeight) + 2 * Widths.space.card)

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Theme.rule
        }

        Text {
            id: said

            objectName: "review-footer-text"
            anchors.left: parent.left
            anchors.leftMargin: Widths.space.card
            anchors.right: ways.left
            anchors.rightMargin: Widths.space.row
            anchors.verticalCenter: parent.verticalCenter
            text: !frame.visible ? "" : frame.editing ? OrderEditor.footer : Review.footer
            textFormat: Text.PlainText
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
            wrapMode: Text.Wrap
        }

        Row {
            id: ways

            anchors.right: parent.right
            anchors.rightMargin: Widths.space.card
            anchors.verticalCenter: parent.verticalCenter
            spacing: Widths.space.row

            // Back to the cards, and nothing is given up: the frame is a look at what will
            // go, and a look that cost what was typed in it would not be one anybody takes.
            CardAction {
                objectName: "review-back"
                anchors.verticalCenter: parent.verticalCenter
                visible: !frame.editing
                activeFocusOnTab: visible
                label: ImportCaptions.previousLabel
                onTriggered: frame.back()
            }

            // « Retour » out of the order: the draft is let go, nothing is written.
            CardAction {
                objectName: "order-back"
                anchors.verticalCenter: parent.verticalCenter
                visible: frame.editing
                activeFocusOnTab: visible
                label: ImportCaptions.returnLabel
                onTriggered: frame.leaveEditor()
            }

            ActionButton {
                objectName: "order-save"
                anchors.verticalCenter: parent.verticalCenter
                visible: frame.editing
                scaled: true
                height: sized(28)
                ready: OrderEditor.savable
                label: OrderEditor.saveLabel
                onTriggered: {
                    if (OrderEditor.save())
                        frame.leaveEditor()
                }
            }

            ActionButton {
                id: importButton

                objectName: "review-import"
                anchors.verticalCenter: parent.verticalCenter
                visible: !frame.editing
                scaled: true
                height: sized(28)
                // Everything dropped is already there: the one thing left to do is to close,
                // and a button that said « Rien à importer » and did nothing would be a wall.
                // And while a series is stopped: it is what the button answers, below. And once a
                // line has gone: the frame was opened again from the transfers with nothing left
                // to confirm, and the way back to them is this button.
                ready: Review.nothingToImport || Review.importable || ImportTally.blocked > 0
                       || ImportTally.started
                label: Review.nothingToImport ? ImportCaptions.closeLabel : ImportCaptions.nextLabel
                onTriggered: {
                    // The keys off the field last typed in, before the step changes under it.
                    importButton.forceActiveFocus(Qt.OtherFocusReason)
                    // A series that a required value or a duplicate number stops is not the
                    // third step's to meet: the button shows the first of them, field and all,
                    // and stays on this step. It used to send the others on and the window
                    // back to the cards, to a series « À vérifier » the reader had to find.
                    if (ImportTally.blocked > 0 && Review.view.showBlocked())
                        return
                    // Read before the queue moves: what `confirm` lets go leaves the frame,
                    // and an emptied frame has nothing left to say there was nothing.
                    if (Review.nothingToImport) {
                        // What is already there is told to the server, so its session is
                        // cleaned up rather than left in its inbox (`Imports::send`).
                        ImportQueue.confirm()
                        frame.done()
                        return
                    }
                    // Nothing is sent from here: « Importer » is the next step's button, and
                    // the cards are only made ready for it.
                    ImportQueue.prepare()
                    frame.imported()
                }
            }
        }
    }
}
