// One value out of a list too long for pills: a name above, the value held, a list under it.
//
// Pills stay where the answers are three or four words (`SettingsChoice`); nine of them wrapped
// onto two lines and made the series window taller than the screens it is opened on. This is
// the only dropdown in the client, so it is written once and all of it is here: the keys, the
// pointer, the focus and what a screen reader is told.
//
// The keys are those of a native combo box. Closed, Space, Enter, Up, Down and Alt+Down open
// the list; open, the arrows, Home, End, Page Up and Page Down move along it, a letter goes to
// the next word that begins with it (several quickly, to the word that begins with them all),
// Space and Enter take the row pointed at, Escape shuts the list and keeps the value, and Tab
// shuts it, keeps the value and goes on to the next control as it would have from the closed
// field. Rows take the real focus, not a drawn mark that only the eye can follow: a screen
// reader reads the row the keys are on because that row is what has the focus.
//
// An option may say `possible: false` and a `reason`: it is drawn greyed with its reason under
// it, and never taken. Left out of the list, the order that would loop was a choice that
// vanished with nothing said. It is disabled, as `RoleMenu` disables what it cannot do, so a
// screen reader says it unavailable — the state, which it announces, and not only the
// description, which it reads when asked — and the keys pass over it as a native list does: a
// disabled item cannot hold the focus, and a row the keys stopped on with the focus elsewhere
// was a row no screen reader would read.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Leaf

Column {
    id: select

    /// The name above the field, which is also what a screen reader says it is.
    required property string name
    /// `[{ value, label }]`, worded and ordered in C++, as `SettingsChoice` is given them.
    required property var options
    required property int chosen
    /// Whether the name is drawn above. A select standing in a line that already has a caption
    /// of its own keeps the name for the screen reader alone.
    property bool captioned: true
    /// Whether Tab reaches it. The editing windows walk every control with the keys.
    property bool tabStops: true
    /// What the field says while no option is chosen — a way to add one, rather than a value.
    property string placeholder: ""
    /// Edged in `rule` while it is closed and not focused, for a select standing on a surface
    /// of its own colour: in the dark theme `onBar` and `onPaper` are one, and the field of a
    /// step's unit could not be told from the line it stands on.
    property bool edged: false

    signal picked(int value)

    readonly property int chosenAt: {
        for (let at = 0; at < select.options.length; ++at) {
            if (select.options[at].value === select.chosen)
                return at
        }
        return -1
    }
    readonly property string chosenLabel: chosenAt >= 0 ? select.options[chosenAt].label : ""
    readonly property bool opened: list.opened

    /// The row the keys and the pointer are on, a place in `options`.
    property int pointed: -1
    /// The last thing to move `pointed` was a key: only then is the row ringed. Under the
    /// pointer it is tinted, which already says which row a click takes.
    property bool keyed: false
    /// Where the pointer last was over the list, to tell it moving from the list moving.
    property var rested: undefined
    /// Letters typed in the last moment, for the jump to a word.
    property string typed: ""

    width: parent ? parent.width : implicitWidth
    spacing: Widths.space.compact

    /// The keys to the field, as Tab would bring them there.
    function takeKeys(): void {
        field.forceActiveFocus(Qt.TabFocusReason)
    }

    function open(): void {
        select.pointed = select.nearest(Math.max(0, select.chosenAt), 1)
        select.keyed = true
        list.open()
    }

    /// The option at `to` when it may be taken, else the nearest one that may going `toward`
    /// (1 or -1), else the nearest the other way; -1 when none may.
    function nearest(to: int, toward: int): int {
        const count = select.options.length
        const bounded = Math.max(0, Math.min(count - 1, to))
        for (const way of [toward, -toward]) {
            for (let at = bounded; at >= 0 && at < count; at += way) {
                if (select.options[at].possible !== false)
                    return at
            }
        }
        return -1
    }

    function point(to: int, toward: int): void {
        const at = select.nearest(to, toward)
        select.pointed = at
        const row = rows.itemAt(at)
        if (!row) {
            // Nothing that may be taken: the keys stay with the list, so Escape still shuts it.
            scroller.forceActiveFocus(Qt.TabFocusReason)
            return
        }
        row.forceActiveFocus(Qt.TabFocusReason)
        // Scrolled into view when the list is longer than what it shows.
        if (row.y < scroller.contentY)
            scroller.contentY = row.y
        else if (row.y + row.height > scroller.contentY + scroller.height)
            scroller.contentY = row.y + row.height - scroller.height
    }

    function take(at: int): void {
        if (at >= 0 && at < select.options.length && select.options[at].possible === false)
            return
        list.close()
        if (at >= 0 && at < select.options.length)
            select.picked(select.options[at].value)
    }

    /// The word the letters typed so far lead to, starting after the row pointed at so that one
    /// letter pressed again goes on to the next word with that letter.
    function lead(letters: string): int {
        const wanted = letters.toLowerCase()
        const same = wanted.split("").every(one => one === wanted[0])
        const count = select.options.length
        for (let step = same ? 1 : 0; step <= count; ++step) {
            const at = (Math.max(0, select.pointed) + step) % count
            if (select.options[at].possible !== false
                    && select.options[at].label.toLowerCase().startsWith(same ? wanted[0] : wanted))
                return at
        }
        return -1
    }

    Text {
        visible: select.captioned
        textFormat: Text.PlainText
        text: select.name
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        font.weight: Font.Medium
    }

    Rectangle {
        id: field

        width: parent.width
        height: Widths.scale.size(34)
        radius: 6
        color: pointer.hovered && !select.opened ? Theme.onPaper : Theme.onBar
        border.color: select.opened || activeFocus ? Theme.emerald
                    : select.edged ? Theme.rule : "transparent"
        border.width: 1
        activeFocusOnTab: select.tabStops

        Behavior on color {
            ColorAnimation { duration: 90 }
        }

        Accessible.role: Accessible.ComboBox
        Accessible.name: select.name
        Accessible.description: select.chosenAt >= 0 ? select.chosenLabel : select.placeholder
        Accessible.focusable: true
        Accessible.onPressAction: select.open()

        Keys.onPressed: event => {
            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                    || event.key === Qt.Key_Enter || event.key === Qt.Key_Down
                    || event.key === Qt.Key_Up) {
                if (!event.isAutoRepeat)
                    select.open()
                event.accepted = true
            }
        }

        HoverHandler {
            id: pointer

            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: {
                field.forceActiveFocus(Qt.MouseFocusReason)
                if (select.opened)
                    list.close()
                else {
                    select.open()
                    select.keyed = false
                }
            }
        }

        FocusRing {
            objectName: select.objectName + "-focus"
            cornerRadius: field.radius
        }

        Text {
            objectName: select.objectName + "-text"
            anchors.left: parent.left
            anchors.leftMargin: Widths.scale.size(10)
            anchors.right: chevron.left
            anchors.rightMargin: Widths.space.tight
            anchors.verticalCenter: parent.verticalCenter
            text: select.chosenAt >= 0 ? select.chosenLabel : select.placeholder
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: select.chosenAt >= 0 ? Theme.ink : Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(14)
        }

        Glyph {
            id: chevron

            anchors.right: parent.right
            anchors.rightMargin: Widths.scale.size(8)
            anchors.verticalCenter: parent.verticalCenter
            side: Widths.scale.icon(16)
            source: "assets/icons/expand_more.svg"
            tint: Theme.inkFaint
            rotation: select.opened ? 180 : 0
        }

        Popup {
            id: list

            objectName: select.objectName + "-list"
            // Under the field, or over it when the window has more room above: a list
            // taller than what is left below was pushed up by its margins and drew itself
            // over the field it belongs to.
            y: list.upward ? -list.height - Widths.space.compact
                           : field.height + Widths.space.compact
            // As wide as its longest word, in the bold a chosen row is drawn in, and never
            // narrower than the field: at the field's width, « Chapitres » chosen in a column
            // as narrow as « Compté en » read « Chapitr… ».
            width: Math.max(field.width, list.widest + 2 * Widths.scale.size(14)
                                         + 2 * list.padding + 1)
            readonly property real widest: {
                let most = 0
                for (const one of select.options)
                    most = Math.max(most, bold.advanceWidth(one.label))
                return Math.ceil(most)
            }
            padding: Widths.space.compact

            FontMetrics {
                id: bold

                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.body(14)
                font.weight: Font.Medium
            }
            margins: Widths.space.gutter
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
            // Taken, so that the keys reach the rows and Escape shuts the list before the
            // window it stands in.
            focus: true

            /// Room left under and over the field, taken when the list opens: where the
            /// window ends is not a thing that moves while it is open.
            property bool upward: false
            property real room: 0

            onAboutToShow: {
                const top = field.mapToItem(null, 0, 0).y
                const below = field.Window.height - (top + field.height)
                            - Widths.space.compact - list.margins
                const above = top - Widths.space.compact - list.margins
                const wanted = column.height + 2 * list.padding
                list.upward = below < wanted && above > below
                list.room = (list.upward ? above : below) - 2 * list.padding
            }

            onOpened: select.point(select.pointed, 1)
            // The value stays with the field, where the keys were: the popup would otherwise
            // leave the focus on nothing.
            onClosed: {
                select.typed = ""
                select.rested = undefined
                field.forceActiveFocus(Qt.PopupFocusReason)
            }

            background: Rectangle {
                radius: Theme.buttonRadius
                color: Theme.surface
                border.color: Theme.rule
                border.width: 1

                CardLift { level: CardLift.Menu }
            }

            contentItem: Flickable {
                id: scroller

                // Eight rows at most; the ninth is reached by the keys or the wheel.
                implicitHeight: Math.min(column.height, list.room,
                                         8 * (Widths.scale.size(40) + Widths.space.tight))
                contentHeight: column.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Accessible.role: Accessible.List
                Accessible.name: select.name

                Timer {
                    id: forgetting

                    interval: 800
                    onTriggered: select.typed = ""
                }

                Keys.onPressed: event => {
                    const last = select.options.length - 1
                    const page = 7
                    let to = -1
                    let toward = 1
                    if (event.key === Qt.Key_Down)
                        to = select.pointed + 1
                    else if (event.key === Qt.Key_Up)
                        [to, toward] = [select.pointed - 1, -1]
                    else if (event.key === Qt.Key_Home)
                        to = 0
                    else if (event.key === Qt.Key_End)
                        [to, toward] = [last, -1]
                    else if (event.key === Qt.Key_PageDown)
                        to = select.pointed + page
                    else if (event.key === Qt.Key_PageUp)
                        [to, toward] = [select.pointed - page, -1]
                    if (to >= 0 || event.key === Qt.Key_Down || event.key === Qt.Key_Up
                            || event.key === Qt.Key_Home || event.key === Qt.Key_PageUp) {
                        select.keyed = true
                        select.point(to, toward)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                               || event.key === Qt.Key_Enter) {
                        if (!event.isAutoRepeat)
                            select.take(select.pointed)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                        const forward = event.key === Qt.Key_Tab
                        list.close()
                        const next = field.nextItemInFocusChain(forward)
                        if (next)
                            next.forceActiveFocus(forward ? Qt.TabFocusReason
                                                          : Qt.BacktabFocusReason)
                        event.accepted = true
                    // A printable letter only: Escape carries a text of its own (U+001B), and
                    // taking it for a letter kept the list open under the key meant to shut it.
                    } else if (event.text.length === 1 && event.text.charCodeAt(0) > 32
                               && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                        select.typed += event.text
                        forgetting.restart()
                        const at = select.lead(select.typed)
                        if (at >= 0) {
                            select.keyed = true
                            select.point(at, 1)
                        }
                        event.accepted = true
                    }
                }

                Column {
                    id: column

                    width: scroller.width
                    spacing: Widths.space.tight

                    Repeater {
                        id: rows

                        model: select.options

                        delegate: Rectangle {
                            id: row

                            required property var modelData
                            required property int index

                            readonly property bool lit: select.chosen === modelData.value
                            readonly property bool here: select.pointed === index
                            readonly property bool possible: modelData.possible !== false
                            readonly property string reason: modelData.reason ?? ""

                            objectName: select.objectName + "-" + modelData.value
                            // Said unavailable, and passed over by the keys (see the head).
                            enabled: row.possible
                            width: column.width
                            height: Math.max(Widths.scale.size(40), said.implicitHeight
                                             + (because.visible ? because.implicitHeight
                                                                  + Widths.space.tight : 0)
                                             + 2 * Widths.scale.size(8))
                            radius: 9
                            color: here ? Theme.onPaper : (lit ? Theme.emeraldWash : "transparent")
                            border.color: lit ? Theme.emerald : "transparent"
                            border.width: lit ? 1 : 0

                            Behavior on color {
                                ColorAnimation { duration: 90 }
                            }

                            Accessible.role: Accessible.ListItem
                            Accessible.name: modelData.label
                            Accessible.description: row.reason
                            Accessible.selectable: row.possible
                            Accessible.selected: lit
                            Accessible.focusable: true
                            Accessible.onPressAction: select.take(row.index)

                            HoverHandler {
                                cursorShape: row.possible ? Qt.PointingHandCursor
                                                          : Qt.ArrowCursor
                                // The pointer moved, and not the list under it: a list opened
                                // or scrolled under a still pointer re-sends where it is, and
                                // each key then lost the row it had gone to to the row under
                                // the pointer. Measured by where the pointer is on the window.
                                onPointChanged: {
                                    const where = point.scenePosition
                                    // The first position seen after the list opens is where
                                    // the pointer already was, not a move: the row under it took
                                    // the row the keys had gone to, whenever the event came.
                                    const moved = select.rested !== undefined
                                        && (Math.abs(where.x - select.rested.x) > 2
                                            || Math.abs(where.y - select.rested.y) > 2)
                                    select.rested = where
                                    if (hovered && moved && select.pointed !== row.index) {
                                        select.keyed = false
                                        select.point(row.index, 1)
                                    }
                                }
                            }

                            TapHandler {
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: select.take(row.index)
                            }

                            FocusRing {
                                objectName: row.objectName + "-focus"
                                inside: true
                                cornerRadius: row.radius
                                focused: row.here && select.keyed
                                // Tinted by the row already; the ring is its edge.
                                color: "transparent"
                            }

                            Column {
                                anchors.left: parent.left
                                anchors.leftMargin: Widths.scale.size(14)
                                anchors.right: parent.right
                                anchors.rightMargin: Widths.scale.size(14)
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Widths.space.tight

                                Text {
                                    id: said

                                    width: parent.width
                                    text: row.modelData.label
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    color: !row.possible ? Theme.inkFaint
                                         : row.lit ? Theme.emerald : Theme.ink
                                    font.family: Theme.textFamily
                                    font.pixelSize: Widths.scale.body(14)
                                    font.weight: row.lit ? Font.Medium : Font.Normal
                                }

                                Text {
                                    id: because

                                    objectName: row.objectName + "-reason"
                                    width: parent.width
                                    visible: row.reason.length > 0
                                    text: row.reason
                                    textFormat: Text.PlainText
                                    wrapMode: Text.WordWrap
                                    color: Theme.inkSoft
                                    font.family: Theme.textFamily
                                    font.pixelSize: Widths.scale.caption(12)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
