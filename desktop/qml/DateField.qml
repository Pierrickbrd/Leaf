// A date typed as JJ/MM/AAAA, or chosen in a calendar, and handed on as the string the
// contract carries.
//
// `publishedOn` is free text to the server, and the files it reads write it as « 2011 »,
// « 2011-03 » or « 2011-03-04 » (a ComicInfo with no month has no day either). A single text
// line took any of them, and « 2011-02-30 » as well: the server stored it and the reader
// showed it back as typed. Three fields, one of them a list of months, were tried and set
// aside as a form to fill; this is one line again, and the day that does not exist is said
// before the button is pressed.
//
// A date may stop short, and the line says so by what is left out: « 1998 », « 10/1998 » and
// « 12/10/1998 » are all dates, a day with no month is not, and nothing at all is the field
// taken away. The calendar chooses a whole day; a partial date is typed.

import QtQuick
import Leaf

Column {
    id: date

    /// The words of the editing windows (`Editing.words`).
    required property var words
    /// What is drawn above the line.
    required property string name

    /// The date as it is sent: « 2011-03-04 », « 2011-03 », « 2011 », or nothing.
    readonly property string value: date.read(line.text).iso
    /// What the date was when it was loaded, in the same shape as `value`, so that a date
    /// the file wrote as « 2011-3-4 » is not sent back for being spelled differently.
    property string initial: ""
    /// Said when the line is left or a day is chosen: the one moment a value is taken, not
    /// every keystroke, which passes through « 12/10/199 » on its way to a date.
    signal committed()
    /// Nothing can be typed or chosen.
    property bool readOnly: false
    /// What the field itself is drawn with, for a sheet whose other boxes are not the dialogs':
    /// edged in the rule, in the sheet's body size, and as tall as the boxes beside it.
    property alias edged: line.edged
    property alias fontSize: line.font.pixelSize
    property alias fieldHeight: line.height
    /// Whether the keys are in the line.
    readonly property bool typing: line.activeFocus
    /// What is wrong with the line, or nothing.
    readonly property string problem: date.read(line.text).problem

    /// The line read as a date: the ISO string it stands for and what is wrong with it.
    function read(text: string): var {
        const raw = text.trim()
        if (raw.length === 0)
            return { "iso": "", "problem": "" }
        const parts = raw.split("/").map(part => part.trim())
        if (parts.length > 3 || !parts.every(part => /^\d*$/.test(part)))
            return { "iso": "", "problem": date.words.dateShape }
        // Read from the right: the year is always last, and what is left out is the day, then
        // the month.
        const year = parts[parts.length - 1]
        const month = parts.length > 1 ? parts[parts.length - 2] : ""
        const day = parts.length === 3 ? parts[0] : ""
        if (year.length === 0)
            return { "iso": "", "problem": date.words.dateNeedsYear }
        if (!/^\d{4}$/.test(year))
            return { "iso": "", "problem": date.words.dateYearInvalid }
        if (month.length === 0) {
            return day.length > 0 ? { "iso": "", "problem": date.words.dateNeedsMonth }
                                  : { "iso": year, "problem": "" }
        }
        if (!/^\d{1,2}$/.test(month) || Number(month) < 1 || Number(month) > 12)
            return { "iso": "", "problem": date.words.dateMonthInvalid }
        const iso = year + "-" + month.padStart(2, "0")
        if (day.length === 0)
            return { "iso": iso, "problem": "" }
        if (!/^\d{1,2}$/.test(day) || Number(day) < 1
                || Number(day) > date.daysIn(Number(month), Number(year)))
            return { "iso": "", "problem": date.words.dateDayInvalid }
        return { "iso": iso + "-" + day.padStart(2, "0"), "problem": "" }
    }

    function daysIn(month: int, year: int): int {
        const leap = year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0)
        return [31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][month - 1]
    }

    /// Writes the line from a date as the server wrote it; what is not a date leaves it
    /// empty, and then not touching it sends nothing.
    function load(iso: string): void {
        const parts = /^(\d{4})(?:-(\d{1,2})(?:-(\d{1,2}))?)?$/.exec(iso ?? "")
        const month = parts && parts[2] ? Number(parts[2]) : 1
        const valid = parts && month >= 1 && month <= 12
        const two = number => String(number).padStart(2, "0")
        let text = ""
        if (valid) {
            text = parts[1]
            if (parts[2])
                text = two(month) + "/" + text
            if (parts[3])
                text = two(Number(parts[3])) + "/" + text
        }
        line.text = text
        date.initial = date.value
    }

    width: parent.width
    spacing: Widths.space.compact

    Text {
        visible: date.name.length > 0
        textFormat: Text.PlainText
        text: date.name
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        font.weight: Font.Medium
    }

    Item {
        id: box

        width: parent.width
        height: line.height

        LeafField {
            id: line

            objectName: date.objectName + "-field"
            width: parent.width
            // Room for the button that stands in the line, on the right.
            rightPadding: Widths.scale.size(40)
            activeFocusOnTab: true
            placeholderText: date.words.dateHint
            maximumLength: 10
            inputMethodHints: Qt.ImhDigitsOnly
            validator: RegularExpressionValidator { regularExpression: /[0-9/]*/ }
            readOnly: date.readOnly
            onEditingFinished: date.committed()

            Accessible.name: date.name
            Accessible.description: date.words.dateHint
        }

        IconButton {
            id: opener

            objectName: date.objectName + "-calendar-button"
            anchors.right: parent.right
            anchors.rightMargin: Widths.scale.size(4)
            anchors.verticalCenter: parent.verticalCenter
            glyph: "calendar_month"
            label: date.words.openCalendar
            activeFocusOnTab: true
            visible: !date.readOnly

            // The button stands inside what the calendar is opened from, so a press on it is
            // never an outside press that shuts the calendar before the tap reads it.
            onTriggered: calendar.opened ? calendar.close() : calendar.begin()
        }

        DateCalendar {
            id: calendar

            objectName: date.objectName + "-calendar"
            words: date.words
            // A whole date is the day shown as chosen; a partial one only says which month to
            // open on.
            selected: /^\d{4}-\d{2}-\d{2}$/.test(date.value) ? date.value : ""
            around: date.value

            onPicked: (year, month, day) => {
                const two = number => String(number).padStart(2, "0")
                line.text = two(day) + "/" + two(month) + "/" + year
                date.committed()
            }
            onClosed: line.forceActiveFocus(Qt.PopupFocusReason)
        }
    }

    Text {
        objectName: date.objectName + "-problem"
        width: parent.width
        visible: date.problem.length > 0
        textFormat: Text.PlainText
        text: date.problem
        color: Theme.alert
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        wrapMode: Text.WordWrap

        Accessible.role: Accessible.AlertMessage
        Accessible.name: text
    }
}
