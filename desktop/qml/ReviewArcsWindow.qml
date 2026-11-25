// The window where a series' arcs and sagas are written.
//
// One list, its sagas as separators between the arcs (`Sagas`). The window drew them as a
// tree whose saga carried a range typed by hand and named its first and last arc: it said
// 2–194 while its arcs said 2–23 and 136–194, and moving where a saga starts meant retyping two
// names. Here an arc belongs to the separator above it, the arcs sort themselves by their start
// and are never moved, and a separator slides — dragged by its handle, or chosen and moved with
// ↑ ↓, or placed by « Commencer à l’arc… ». Every edit goes through `Review.edits.arcRowsEdited`: the
// window sorts nothing and counts nothing itself.
//
// Edited on a copy and kept only by « Garder les arcs »: a line half typed is a range that runs
// backwards for a moment, and the card must not be told so at each keystroke. A line is sorted
// when it is left, not while it is typed in (`regap`). The model has the last word
// (`Review.edits.setArcs`) and the window says so rather than closing on a refusal.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Leaf

LeafDialog {
    id: window

    /// What the sheet holds, the editions its pages are given for, and the sheet it is on.
    property var arcs: []
    property var editions: []
    property string sheetId: ""
    property bool refused: false

    /// The rows being edited, plain JS objects so that a box typed into changes its own row
    /// without the list being drawn again under the hand.
    property var rows: []
    /// Which edition's pages the last column gives.
    property int edition: 0
    /// The separator chosen, by its `uid`: its index changes with every edit.
    property int chosen: -1
    /// The arc whose box last had the focus: a separator added goes above it.
    property int focusedArc: -1
    property int nextUid: 0
    /// The box to give the focus back to once the rows are drawn again.
    property var refocus: null

    readonly property var words: SheetCaptions.sheetWords
    readonly property int chosenAt: window.indexOf(window.chosen)
    readonly property int sagaCount: {
        const names = {}
        for (const one of window.rows) {
            if (one.kind === "saga")
                names[one.name.trim()] = true
        }
        return Object.keys(names).length
    }
    readonly property int arcCount: window.rows.filter(one => one.kind === "arc").length

    function plain(list): var {
        return Array.from(list, one => {
            const row = Object.assign({}, one)
            if (row.uid === undefined && row.kind !== "gap")
                row.uid = window.nextUid++
            return row
        })
    }

    function open() {
        window.rows = window.plain(Review.edits.arcRows(window.arcs))
        window.edition = 0
        window.chosen = -1
        window.focusedArc = -1
        window.refocus = null
        window.refused = false
        window.visible = true
    }

    function indexOf(uid: int): int {
        for (let at = 0; at < window.rows.length; ++at) {
            if (window.rows[at].uid === uid)
                return at
        }
        return -1
    }

    /// One edit, through the model, with the focus kept on what had it.
    function edited(how: string, at: int, arg: int) {
        const now = scroller.Window.activeFocusItem
        if (window.refocus === null && now && now.rowUid !== undefined)
            window.refocus = { uid: now.rowUid, field: now.fieldName }
        window.rows = window.plain(Review.edits.arcRowsEdited(window.rows, how, at, arg))
        Qt.callLater(() => { window.refocus = null })
    }

    /// Called by every box of a row as it is drawn: the one the focus was on takes it back.
    function claim(item) {
        if (window.refocus && window.refocus.uid === item.rowUid
                && window.refocus.field === item.fieldName)
            item.forceActiveFocus(Qt.OtherFocusReason)
    }

    /// A separator above the arc last typed in, or at the top: « + Une saga », « + Aucune saga ».
    function addSeparator(kind: string) {
        const uid = window.nextUid++
        const copy = window.rows.slice()
        const above = window.indexOf(window.focusedArc)
        copy.splice(above >= 0 ? above : 0, 0, kind === "saga" ? { kind: "saga", name: "", uid: uid }
                                                                : { kind: "none", uid: uid })
        window.rows = copy
        window.chosen = uid
        window.refocus = { uid: uid, field: kind === "saga" ? "name" : "separator" }
        window.edited("regap", -1, 0)
    }

    objectName: "review-arcs-window"
    surfaceName: "review-arcs-surface"
    drawnWidth: 820

    // The widths of the columns, shared by the head and every arc so that they line up.
    readonly property int handleWidth: Widths.scale.size(14)
    readonly property int unitWidth: Widths.scale.fit(118)
    readonly property int boundWidth: Widths.scale.fit(58)
    readonly property int removeWidth: Widths.scale.size(28)
    readonly property int gap: Widths.space.row
    readonly property real flexible: Math.max(0, width - leftPadding - rightPadding
                                                 - handleWidth - unitWidth - 2 * boundWidth
                                                 - removeWidth - 6 * gap - Widths.space.compact)
    readonly property real nameWidth: Math.round(window.flexible * 0.52)
    readonly property real fallsWidth: window.flexible - window.nameWidth

    Row {
        width: parent.width
        spacing: Widths.space.row

        Text {
            id: heading

            textFormat: Text.PlainText
            text: window.words.arcsWindowTitle
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: Widths.scale.title(20)
            font.weight: Font.Bold
        }

        Text {
            objectName: "review-arcs-count"
            anchors.baseline: heading.baseline
            textFormat: Text.PlainText
            text: SheetCaptions.arcsCountLabel(window.sagaCount, window.arcCount)
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(13)
        }
    }

    // Shown from two editions: with one, there is nothing to choose the pages of.
    Row {
        visible: window.editions.length >= 2
        spacing: Widths.space.row

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: window.words.arcsEditionPick
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
        }

        LeafSelect {
            objectName: "review-arcs-edition"
            width: Widths.scale.fit(220)
            name: window.words.arcsEditionPick
            captioned: false
            edged: true
            chosen: window.edition
            options: window.editions.map((one, at) => ({ value: at, label: one.name }))
            onPicked: value => window.edition = value
        }
    }

    Row {
        spacing: window.gap
        leftPadding: window.handleWidth + window.gap

        component Head: Text {
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(11)
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.6
        }

        Head { width: window.nameWidth; text: window.words.arcsArc }
        Head { width: window.unitWidth; text: window.words.arcsUnit }
        Head { width: window.boundWidth; text: window.words.arcsFrom }
        Head { width: window.boundWidth; text: window.words.arcsTo }
        Head { width: window.fallsWidth; text: window.words.arcsInEdition }
    }

    Flickable {
        id: scroller

        width: parent.width
        height: Math.min(contentHeight, Widths.scale.fit(420))
        contentHeight: lines.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: lines

            width: scroller.width - Widths.space.compact
            spacing: Widths.space.tight

            Repeater {
                id: repeater

                model: window.rows

                delegate: Loader {
                    id: line

                    required property int index
                    required property var modelData

                    width: lines.width
                    sourceComponent: line.modelData.kind === "arc" ? arcLine
                                     : line.modelData.kind === "gap" ? gapLine : separatorLine
                }
            }
        }
    }

    Component {
        id: gapLine

        Text {
            readonly property var row: (parent as Loader)?.modelData ?? {}

            leftPadding: window.handleWidth + window.gap
            topPadding: Widths.space.tight
            bottomPadding: Widths.space.tight
            textFormat: Text.PlainText
            text: row.said ?? ""
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            font.italic: true
        }
    }

    Component {
        id: arcLine

        Row {
            id: arc

            readonly property Loader holder: parent as Loader
            readonly property var row: arc.holder?.modelData ?? {}
            readonly property int at: arc.holder?.index ?? -1
            property bool moved: false

            spacing: window.gap
            leftPadding: window.handleWidth + window.gap

            /// The row itself in `window.rows`, which a box typed into changes in place.
            function set(key: string, value) {
                window.rows[arc.at][key] = value
            }

            function took(item) {
                if (item.activeFocus)
                    window.focusedArc = arc.row.uid
            }

            LeafField {
                readonly property int rowUid: arc.row.uid
                readonly property string fieldName: "name"

                objectName: "review-arc-name-" + arc.at
                scaled: true
                width: window.nameWidth
                activeFocusOnTab: true
                text: arc.row.name ?? ""
                placeholderText: window.words.arcsName
                onTextEdited: arc.set("name", text)
                onActiveFocusChanged: arc.took(this)
                Component.onCompleted: window.claim(this)
            }

            LeafSelect {
                readonly property int rowUid: arc.row.uid
                readonly property string fieldName: "unit"

                objectName: "review-arc-unit-" + arc.at
                width: window.unitWidth
                anchors.verticalCenter: parent.verticalCenter
                name: window.words.arcsUnit
                captioned: false
                edged: true
                chosen: arc.row.unit === "VOLUME" ? 1 : 0
                options: [{ value: 0, label: window.words.arcsChapters },
                          { value: 1, label: window.words.arcsVolumes }]
                onPicked: value => {
                    arc.set("unit", value === 1 ? "VOLUME" : "CHAPTER")
                    window.refocus = { uid: arc.row.uid, field: "unit" }
                    window.edited("regap", -1, 0)
                }
                Component.onCompleted: window.claim(this)
            }

            LeafField {
                readonly property int rowUid: arc.row.uid
                readonly property string fieldName: "from"

                objectName: "review-arc-from-" + arc.at
                scaled: true
                width: window.boundWidth
                activeFocusOnTab: true
                text: String(arc.row.from ?? "")
                placeholderText: window.words.arcsFrom
                validator: IntValidator { bottom: 0 }
                onTextEdited: {
                    arc.set("from", text)
                    arc.moved = true
                }
                // Sorted once left, and not at each figure: « 1 » then « 15 » then « 150 » was
                // the line jumping three times under the hand.
                onEditingFinished: if (arc.moved) Qt.callLater(window.edited, "regap", -1, 0)
                onActiveFocusChanged: arc.took(this)
                Component.onCompleted: window.claim(this)
            }

            LeafField {
                readonly property int rowUid: arc.row.uid
                readonly property string fieldName: "to"

                objectName: "review-arc-to-" + arc.at
                scaled: true
                width: window.boundWidth
                activeFocusOnTab: true
                text: String(arc.row.to ?? "")
                placeholderText: window.words.arcsTo
                validator: IntValidator { bottom: 0 }
                onTextEdited: {
                    arc.set("to", text)
                    arc.moved = true
                }
                onEditingFinished: if (arc.moved) Qt.callLater(window.edited, "regap", -1, 0)
                onActiveFocusChanged: arc.took(this)
                Component.onCompleted: window.claim(this)
            }

            Text {
                objectName: "review-arc-falls-" + arc.at
                width: window.fallsWidth
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: (arc.row.falls ?? [])[window.edition] ?? ""
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                elide: Text.ElideRight
            }

            IconButton {
                readonly property int rowUid: arc.row.uid
                readonly property string fieldName: "remove"

                objectName: "review-arc-remove-" + arc.at
                anchors.verticalCenter: parent.verticalCenter
                width: window.removeWidth
                height: width
                side: Widths.scale.icon(16)
                glyph: "close"
                label: window.words.arcsRemove
                activeFocusOnTab: true
                onTriggered: window.edited("remove", arc.at, 0)
            }
        }
    }

    Component {
        id: separatorLine

        // The separator itself takes the focus: chosen by it, moved by ↑ ↓ while it has it.
        FocusScope {
            id: separator

            readonly property Loader holder: parent as Loader
            readonly property var row: separator.holder?.modelData ?? {}
            readonly property int at: separator.holder?.index ?? -1
            readonly property bool saga: row.kind === "saga"
            readonly property bool isChosen: window.chosen === row.uid
            readonly property int rowUid: row.uid ?? -1
            readonly property string fieldName: "separator"
            readonly property bool keyStop: true
            /// How far the hand has carried it, drawn and not yet placed.
            property real lift: 0

            objectName: "review-separator-" + separator.at
            width: parent ? parent.width : 0
            height: band.height
            z: separator.lift !== 0 ? 2 : 0
            activeFocusOnTab: true
            transform: Translate { y: separator.lift }

            Accessible.role: Accessible.ListItem
            Accessible.name: separator.saga ? (separator.row.name ?? "") : window.words.arcsNoSaga
            Accessible.description: separator.row.said ?? ""

            onActiveFocusChanged: if (activeFocus) window.chosen = separator.row.uid
            Component.onCompleted: window.claim(this)

            Keys.onUpPressed: event => {
                window.refocus = { uid: separator.row.uid, field: "separator" }
                window.edited("move", separator.at, -1)
                event.accepted = true
            }
            Keys.onDownPressed: event => {
                window.refocus = { uid: separator.row.uid, field: "separator" }
                window.edited("move", separator.at, 1)
                event.accepted = true
            }

            /// The arc nearest to where it was let go: the one it now starts at.
            function dropped() {
                const middle = separator.holder.y + separator.height / 2 + separator.lift
                let best = -1
                let distance = Infinity
                for (let at = 0; at < repeater.count; ++at) {
                    const item = repeater.itemAt(at)
                    if (!item || window.rows[at].kind !== "arc")
                        continue
                    const away = Math.abs(item.y + item.height / 2 - middle)
                    if (away < distance) {
                        distance = away
                        best = at
                    }
                }
                separator.lift = 0
                if (best >= 0) {
                    window.refocus = { uid: separator.row.uid, field: "separator" }
                    window.edited("start", separator.at, best)
                }
            }

            Rectangle {
                id: band

                width: parent.width
                height: Math.max(Widths.scale.fit(40),
                                 content.implicitHeight + 2 * Widths.space.tight)
                // Between a field's 6 and a button's 12, as drawn: a line of the list, not a card.
                radius: 8
                color: separator.saga ? Theme.onPaper : "transparent"
                border.width: separator.isChosen ? 1 : 0
                border.color: Theme.emerald

                // « Aucune saga » is edged in dashes: a line that ends a saga and holds nothing
                // of its own. Qt Quick draws no dashed border, so the edge is painted.
                Canvas {
                    anchors.fill: parent
                    visible: !separator.saga && !separator.isChosen
                    onPaint: {
                        const context = getContext("2d")
                        context.reset()
                        context.setLineDash([3, 2])
                        context.lineWidth = 1
                        context.strokeStyle = Theme.rule
                        context.beginPath()
                        context.roundedRect(0.5, 0.5, width - 1, height - 1, band.radius,
                                            band.radius)
                        context.stroke()
                    }
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: separator.forceActiveFocus(Qt.MouseFocusReason)
                }

                Row {
                    id: content

                    anchors.left: parent.left
                    anchors.leftMargin: Widths.space.tight
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: window.gap

                    Glyph {
                        objectName: "review-separator-handle-" + separator.at
                        anchors.verticalCenter: parent.verticalCenter
                        side: window.handleWidth
                        source: "assets/icons/drag_indicator.svg"
                        tint: Theme.inkFaint

                        MouseArea {
                            property real start: 0

                            anchors.fill: parent
                            anchors.margins: -Widths.space.tight
                            cursorShape: Qt.SizeVerCursor
                            onPressed: mouse => {
                                start = mapToItem(lines, mouse.x, mouse.y).y
                                separator.forceActiveFocus(Qt.MouseFocusReason)
                            }
                            onPositionChanged: mouse => {
                                separator.lift = mapToItem(lines, mouse.x, mouse.y).y - start
                            }
                            onReleased: separator.dropped()
                            onCanceled: separator.lift = 0
                        }
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Widths.scale.size(8)
                        height: width
                        radius: width / 2
                        color: separator.saga ? Theme.emerald : "transparent"
                        border.width: separator.saga ? 0 : 1
                        border.color: Theme.inkFaint
                    }

                    LeafField {
                        readonly property int rowUid: separator.rowUid
                        readonly property string fieldName: "name"

                        objectName: "review-saga-name-" + separator.at
                        scaled: true
                        visible: separator.saga
                        width: Math.min(Widths.scale.fit(300), window.nameWidth)
                        activeFocusOnTab: true
                        text: separator.row.name ?? ""
                        placeholderText: window.words.arcsName
                        font.weight: Font.DemiBold
                        onTextEdited: window.rows[separator.at].name = text
                        onActiveFocusChanged: if (activeFocus) window.chosen = separator.row.uid
                        Component.onCompleted: window.claim(this)
                    }

                    Text {
                        visible: !separator.saga
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: window.words.arcsNoSaga
                        color: Theme.inkSoft
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(13)
                    }

                    Text {
                        objectName: "review-saga-span-" + separator.at
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, content.width - x - window.removeWidth - window.gap
                                           - Widths.space.tight)
                        textFormat: Text.PlainText
                        text: separator.row.said ?? ""
                        color: Theme.inkSoft
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.caption(12)
                        elide: Text.ElideRight
                    }

                    IconButton {
                        objectName: "review-separator-remove-" + separator.at
                        anchors.verticalCenter: parent.verticalCenter
                        width: window.removeWidth
                        height: width
                        side: Widths.scale.icon(16)
                        glyph: "close"
                        label: window.words.arcsRemove
                        activeFocusOnTab: true
                        onTriggered: {
                            if (window.chosen === separator.row.uid)
                                window.chosen = -1
                            window.edited("remove", separator.at, 0)
                        }
                    }
                }
            }
        }
    }

    Flow {
        width: parent.width
        spacing: Widths.space.compact

        ActionButton {
            objectName: "review-arc-add"
            scaled: true
            label: window.words.arcsAdd
            onTriggered: {
                const uid = window.nextUid++
                window.refocus = { uid: uid, field: "name" }
                window.rows = window.rows.concat([{ kind: "arc", name: "", unit: "CHAPTER",
                                                    from: "", to: "", falls: [], uid: uid }])
                window.focusedArc = uid
                Qt.callLater(() => {
                    window.refocus = null
                    scroller.contentY = Math.max(0, scroller.contentHeight - scroller.height)
                })
            }
        }

        QuietButton {
            objectName: "review-saga-add"
            scaled: true
            label: window.words.arcsAddSaga
            onTriggered: window.addSeparator("saga")
        }

        QuietButton {
            objectName: "review-none-add"
            scaled: true
            label: window.words.arcsAddNone
            onTriggered: window.addSeparator("none")
        }

        // The hint ends on « ou » and the button is what it leads to: the two wrap together,
        // or at 820 px the sentence stops on « ou » and its button stands alone on the next line.
        Row {
            visible: window.chosenAt >= 0
            spacing: Widths.space.compact

            Text {
                height: start.height
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText
                text: window.words.arcsMoveHint
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
            }

            QuietButton {
                id: start

                objectName: "review-separator-start"
                scaled: true
                label: window.words.arcsStartAt
                onTriggered: startMenu.open()

                LeafMenu {
                    id: startMenu

                    objectName: "review-separator-start-menu"
                    y: start.height + Widths.space.compact

                    Repeater {
                        model: startMenu.visible ? window.rows.map((one, at) => ({ one: one, at: at }))
                                                           .filter(pair => pair.one.kind === "arc")
                                                 : []

                        delegate: LeafMenuItem {
                            required property var modelData

                            text: modelData.one.name
                            onTriggered: {
                                window.refocus = { uid: window.chosen, field: "separator" }
                                window.edited("start", window.chosenAt, modelData.at)
                            }
                        }
                    }
                }
            }
        }
    }

    Text {
        width: parent.width
        textFormat: Text.PlainText
        text: window.refused ? window.words.arcsRefused : window.words.arcsNote
        color: window.refused ? Theme.alert : Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        wrapMode: Text.WordWrap
    }

    commands: [
        LeafTextAction {
            objectName: "review-arcs-cancel"
            scaled: true
            label: ImportCaptions.cancelLabel
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: window.visible = false
        },

        ActionButton {
            objectName: "review-arcs-keep"
            scaled: true
            label: window.words.arcsKeep
            onTriggered: {
                const list = Review.edits.arcsFromRows(window.rows)
                // An empty list is « no arcs », and must not be what a mistake looks like.
                if (list.length === 0 && window.rows.length > 0)
                    window.refused = true
                else if (Review.edits.setArcs(window.sheetId, list))
                    window.visible = false
                else
                    window.refused = true
            }
        }
    ]
}
