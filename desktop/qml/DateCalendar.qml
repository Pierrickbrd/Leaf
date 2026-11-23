// A month to choose a day from, for the date of a volume.
//
// Qt Quick has no date picker, and `MonthGrid` draws numbers that cannot take the focus, so
// the keys would have had nothing to stand on. The grid is drawn here: six weeks of seven,
// Monday first, the days of the neighbouring months dimmed but chosen all the same.
//
// The albums go back decades, and thirty clicks on an arrow is not a way to reach 1987: the
// year is a line of its own in the header, typed over, with arrows for a year at a time.
//
// The keys are those of a calendar: the arrows move a day or a week, Page Up and Page Down a
// month (with Ctrl, a year), Enter and Space take the day, Escape shuts it and keeps what the
// field held. The pointer only tints the day under it and never moves the day the keys are
// on: a list that let a still pointer take the row the keys had chosen was a defect once
// (`LeafSelect`), and a hover has no business moving anything here.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Leaf

Popup {
    id: calendar

    /// The words of the editing windows (`Editing.words`).
    required property var words
    /// The day shown as chosen, « 2011-03-04 », or nothing when the field holds no whole date.
    property string selected: ""
    /// The month to show when nothing is chosen: « 1998 » or « 1998-10 » are as far as a
    /// partial date goes.
    property string around: ""

    /// The day the keys are on. Always in the month shown: the month is read from it.
    property var cursor: new Date()
    readonly property var today: new Date()
    readonly property real cell: Widths.scale.size(36)
    /// Room left under and over the field, taken when the calendar opens.
    property bool upward: false

    signal picked(int year, int month, int day)

    function daysIn(year: int, month: int): int {
        return new Date(year, month + 1, 0).getDate()
    }

    function pad(number: int): string {
        return String(number).padStart(2, "0")
    }

    /// Opens on the chosen day, else on the month the partial date names, else on today.
    function begin(): void {
        const whole = /^(\d{4})-(\d{2})-(\d{2})$/.exec(calendar.selected)
        const part = /^(\d{4})(?:-(\d{2}))?/.exec(calendar.around)
        const now = new Date()
        if (whole)
            calendar.cursor = new Date(Number(whole[1]), Number(whole[2]) - 1, Number(whole[3]))
        else if (part)
            calendar.cursor = new Date(Number(part[1]), part[2] ? Number(part[2]) - 1 : 0, 1)
        else
            calendar.cursor = now
        calendar.open()
    }

    /// The cursor moved by days, which may cross into another month.
    function walk(days: int): void {
        const at = calendar.cursor
        calendar.cursor = new Date(at.getFullYear(), at.getMonth(), at.getDate() + days)
    }

    /// The cursor moved by months, its day kept unless the month is shorter.
    function leap(months: int): void {
        const at = calendar.cursor
        const first = new Date(at.getFullYear(), at.getMonth() + months, 1)
        const day = Math.min(at.getDate(), calendar.daysIn(first.getFullYear(), first.getMonth()))
        calendar.cursor = new Date(first.getFullYear(), first.getMonth(), day)
    }

    function take(day: var): void {
        calendar.close()
        calendar.picked(day.getFullYear(), day.getMonth() + 1, day.getDate())
    }

    function nameOf(day: var): string {
        return (day.getDate() === 1 ? "1er" : String(day.getDate())) + " "
                + calendar.words.months[day.getMonth()].toLowerCase() + " " + day.getFullYear()
    }

    // Under the field, or over it when the window has more room above, as `LeafSelect`.
    y: calendar.upward ? -calendar.height - Widths.space.compact
                       : (parent ? parent.height : 0) + Widths.space.compact
    width: 7 * calendar.cell + 2 * padding
    padding: Widths.space.compact
    margins: Widths.space.gutter
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    focus: true

    onAboutToShow: {
        const top = parent.mapToItem(null, 0, 0).y
        const below = parent.Window.height - (top + parent.height) - Widths.space.compact
                      - calendar.margins
        const above = top - Widths.space.compact - calendar.margins
        calendar.upward = below < calendar.implicitHeight && above > below
    }
    onOpened: grid.forceActiveFocus(Qt.PopupFocusReason)

    background: Rectangle {
        radius: Theme.buttonRadius
        color: Theme.surface
        border.color: Theme.rule
        border.width: 1

        CardLift { level: CardLift.Menu }
    }

    contentItem: Column {
        spacing: Widths.space.tight

        Accessible.role: Accessible.Dialog
        Accessible.name: calendar.words.calendar

        Row {
            id: header

            readonly property real step: Widths.scale.size(26)

            width: parent.width
            height: header.step
            spacing: 2

            IconButton {
                glyph: "chevron_left"
                label: calendar.words.previousMonth
                width: header.step
                height: header.step
                onTriggered: calendar.leap(-1)
            }
            Text {
                width: header.width - 5 * header.spacing - 4 * header.step - yearLine.width
                height: header.step
                textFormat: Text.PlainText
                text: calendar.words.months[calendar.cursor.getMonth()]
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.body(14)
                font.weight: Font.Medium
            }
            IconButton {
                glyph: "chevron_right"
                label: calendar.words.nextMonth
                width: header.step
                height: header.step
                onTriggered: calendar.leap(1)
            }
            IconButton {
                glyph: "keyboard_double_arrow_left"
                label: calendar.words.previousYear
                width: header.step
                height: header.step
                onTriggered: calendar.leap(-12)
            }
            LeafField {
                id: yearLine

                objectName: calendar.objectName + "-year"
                width: Widths.scale.size(56)
                height: header.step
                activeFocusOnTab: true
                text: String(calendar.cursor.getFullYear())
                maximumLength: 4
                horizontalAlignment: TextInput.AlignHCenter
                inputMethodHints: Qt.ImhDigitsOnly
                validator: RegularExpressionValidator { regularExpression: /[0-9]*/ }

                Accessible.name: calendar.words.calendarYear

                // A year is shown once it has its four digits; fewer are a year on the way.
                onTextEdited: {
                    if (/^[1-9]\d{3}$/.test(text)) {
                        const at = calendar.cursor
                        const day = Math.min(at.getDate(), calendar.daysIn(Number(text), at.getMonth()))
                        calendar.cursor = new Date(Number(text), at.getMonth(), day)
                    }
                }
                Keys.onReturnPressed: grid.forceActiveFocus(Qt.TabFocusReason)
                Keys.onEnterPressed: grid.forceActiveFocus(Qt.TabFocusReason)
            }
            IconButton {
                glyph: "keyboard_double_arrow_right"
                label: calendar.words.nextYear
                width: header.step
                height: header.step
                onTriggered: calendar.leap(12)
            }
        }

        Row {
            Repeater {
                model: calendar.words.weekdays

                delegate: Text {
                    required property string modelData

                    width: calendar.cell
                    height: Widths.scale.size(24)
                    text: modelData
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: Theme.inkFaint
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                    font.weight: Font.Medium
                }
            }
        }

        Item {
            id: grid

            objectName: calendar.objectName + "-grid"
            // The week starts on Monday: the 1st stands that many days into the first row.
            readonly property int lead: (new Date(calendar.cursor.getFullYear(),
                                                  calendar.cursor.getMonth(), 1).getDay() + 6) % 7

            width: 7 * calendar.cell
            height: 6 * calendar.cell
            activeFocusOnTab: true

            Accessible.role: Accessible.Table
            Accessible.name: calendar.words.calendar
            Accessible.description: calendar.nameOf(calendar.cursor)

            Keys.onPressed: event => {
                const ctrl = event.modifiers & Qt.ControlModifier
                if (event.key === Qt.Key_Left)
                    calendar.walk(-1)
                else if (event.key === Qt.Key_Right)
                    calendar.walk(1)
                else if (event.key === Qt.Key_Up)
                    calendar.walk(-7)
                else if (event.key === Qt.Key_Down)
                    calendar.walk(7)
                else if (event.key === Qt.Key_PageUp)
                    calendar.leap(ctrl ? -12 : -1)
                else if (event.key === Qt.Key_PageDown)
                    calendar.leap(ctrl ? 12 : 1)
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                         || event.key === Qt.Key_Space) {
                    if (!event.isAutoRepeat)
                        calendar.take(calendar.cursor)
                } else
                    return
                event.accepted = true
            }

            Repeater {
                model: 42

                delegate: Rectangle {
                    id: day

                    required property int index

                    readonly property var date: new Date(calendar.cursor.getFullYear(),
                                                         calendar.cursor.getMonth(),
                                                         1 - grid.lead + index)
                    readonly property string iso: date.getFullYear() + "-"
                        + calendar.pad(date.getMonth() + 1) + "-" + calendar.pad(date.getDate())
                    readonly property bool here: date.getMonth() === calendar.cursor.getMonth()
                    readonly property bool chosen: iso === calendar.selected
                    readonly property bool now: date.getFullYear() === calendar.today.getFullYear()
                        && date.getMonth() === calendar.today.getMonth()
                        && date.getDate() === calendar.today.getDate()

                    objectName: calendar.objectName + "-" + iso
                    x: (index % 7) * calendar.cell
                    y: Math.floor(index / 7) * calendar.cell
                    width: calendar.cell
                    height: calendar.cell
                    radius: width / 2
                    color: chosen ? Theme.emerald : (pointer.hovered ? Theme.onPaper : "transparent")
                    border.color: now && !chosen ? Theme.emerald : "transparent"
                    border.width: 1

                    Accessible.role: Accessible.Button
                    Accessible.name: calendar.nameOf(date)
                    Accessible.checked: chosen
                    Accessible.onPressAction: calendar.take(date)

                    HoverHandler {
                        id: pointer

                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: calendar.take(day.date)
                    }

                    FocusRing {
                        objectName: day.objectName + "-focus"
                        inside: true
                        cornerRadius: day.radius
                        focused: grid.activeFocus
                                 && day.iso === calendar.cursor.getFullYear() + "-"
                                    + calendar.pad(calendar.cursor.getMonth() + 1) + "-"
                                    + calendar.pad(calendar.cursor.getDate())
                        color: "transparent"
                    }

                    Text {
                        anchors.centerIn: parent
                        textFormat: Text.PlainText
                        text: day.date.getDate()
                        color: day.chosen ? Theme.onEmerald : (day.here ? Theme.ink : Theme.inkFaint)
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(14)
                        font.weight: day.chosen || day.now ? Font.Medium : Font.Normal
                    }
                }
            }
        }
    }
}
