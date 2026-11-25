// One axis of the filter panel: a heading you can fold, and the values under it.
//
// The values are rows and not pills. A pill earns its shape by being one of four on a line
// somebody reads sideways; twenty-eight authors read downwards, and a column of rows gives
// each one the same left edge, which is what makes a long list scannable at all.

import QtQuick
import Leaf

Item {
    id: axisItem

    /// The contract's own spelling, handed straight back to `Shelf.filterBy`.
    required property string axis
    required property string title
    /// `[{ value, label, count }]`, already worded and counted in C++.
    required property var values
    /// How many of them are lit. Held rather than counted here so the heading can say it
    /// while folded — an axis narrowing the shelf must say so without being opened.
    required property int litHere
    required property bool startsOpen

    /// Asked of the panel rather than held, because the answer changes under this item every
    /// time the selection does and a copy here would be a second truth.
    property var lit: (value) => false

    signal picked(string value)

    /// How many values show at once before the axis scrolls inside its own bounds rather
    /// than growing. Five rows is enough to show that there is a list and that it continues;
    /// a library with three hundred authors would otherwise make one axis nine thousand
    /// pixels tall, and build three hundred items to draw the five anybody can see.
    readonly property int atMostOnScreen: 5
    /// A value's row, from its words: a literal 30 round a name that follows the desktop's
    /// text size cut its descenders at half as large again.
    readonly property int rowHeight: Math.max(Widths.scale.size(30),
                                              Math.ceil(valueFont.height) + Widths.space.row)
    /// Where the titles and the values' marks start, past the chevron: one column for both.
    readonly property int indent: Widths.scale.size(28)
    /// Whether the axis gets a field to search itself — and it is the same threshold, on
    /// purpose. It used to be its own number, twelve, which left six to twelve values in the
    /// worst of both: a list you cannot see whole, in a window five rows tall, with nothing
    /// to filter it by. The rule now says itself in one line — if you can see it all you
    /// scan it, if you cannot you can type — and there is no second number to keep in step.
    readonly property bool searchable: values.length > atMostOnScreen
    property bool open: startsOpen
    property string looking: ""

    readonly property var showing: {
        const asked = looking.trim().toLowerCase()
        if (asked.length === 0)
            return values
        const kept = []
        for (const one of values) {
            if (one.label.toLowerCase().indexOf(asked) >= 0)
                kept.push(one)
        }
        return kept
    }

    objectName: "filter-axis-" + axis
    width: parent ? parent.width : 0
    height: head.height + (open ? body.height : 0)

    FontMetrics {
        id: valueFont

        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.body(14)
    }

    Rectangle {
        id: head

        objectName: "filter-axis-head-" + axisItem.axis
        width: parent.width
        height: Math.max(Widths.scale.size(34), axisTitle.implicitHeight + Widths.space.row)
        radius: 8
        color: pointer.hovered ? Theme.onPaper : "transparent"

        /// A stop of the panel's walk — see `KeyWalk`. Enter folds and unfolds it.
        readonly property bool keyStop: true

        Keys.onPressed: event => {
            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                    || event.key === Qt.Key_Enter) {
                axisItem.open = !axisItem.open
                event.accepted = true
            }
        }

        FocusRing {
            objectName: head.objectName + "-focus"
            cornerRadius: head.radius
            inside: true
        }

        HoverHandler {
            id: pointer

            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: axisItem.open = !axisItem.open
        }

        // An icon and not the « ⌄ » and « › » characters it was: those are drawn by the text
        // font, on its own baseline, and the open one sat lower than the closed one and lower
        // than the title beside it. Both symbols share one box, centred on the heading.
        Glyph {
            objectName: "filter-axis-chevron-" + axisItem.axis
            x: Widths.space.compact + (Widths.scale.icon(14) - width) / 2
            anchors.verticalCenter: parent.verticalCenter
            side: Widths.scale.icon(18)
            source: "assets/icons/" + (axisItem.open ? "expand_more" : "chevron_right") + ".svg"
            tint: Theme.inkFaint
        }

        Text {
            id: axisTitle

            objectName: "filter-axis-title-" + axisItem.axis
            anchors.left: parent.left
            anchors.leftMargin: axisItem.indent
            anchors.right: tally.left
            anchors.rightMargin: Widths.space.label
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: axisItem.title
            elide: Text.ElideRight
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(14)
            font.weight: axisItem.litHere > 0 ? Font.Medium : Font.Normal
        }

        // What the axis is doing, without opening it: how many of its values are lit, or
        // how many there are to choose from when none is.
        Text {
            id: tally

            objectName: "filter-axis-tally-" + axisItem.axis
            anchors.right: parent.right
            anchors.rightMargin: Widths.space.row
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: axisItem.litHere > 0 ? String(axisItem.litHere) : String(axisItem.values.length)
            color: axisItem.litHere > 0 ? Theme.emerald : Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
            font.weight: axisItem.litHere > 0 ? Font.Medium : Font.Normal
        }
    }

    Column {
        id: body

        objectName: "filter-axis-body-" + axisItem.axis
        anchors.top: head.bottom
        width: parent.width
        visible: axisItem.open
        spacing: 0

        // Air above and below the field, and only when there is a field. Against the head
        // it read as one control with it — worse under the pointer, where the head takes
        // the same wash as the field's own surface and the two became a single shape.
        Item {
            width: 1
            height: Widths.space.compact
            visible: axisItem.searchable
        }

        LeafSearchLine {
            objectName: "filter-axis-search-" + axisItem.axis
            x: Widths.space.compact
            width: parent.width - 2 * x
            visible: axisItem.searchable
            placeholder: BarCaptions.quotedAxis(axisItem.title)
            onAsked: text => axisItem.looking = text
        }

        Item {
            width: 1
            height: Widths.space.compact
            visible: axisItem.searchable
        }

        // A view and not a repeater: it builds the rows it draws and no more. The axis is
        // as tall as its values up to seven of them, and scrolls beyond that.
        ListView {
            id: rows

            objectName: "filter-axis-rows-" + axisItem.axis
            width: body.width
            height: Math.min(axisItem.showing.length, axisItem.atMostOnScreen)
                    * axisItem.rowHeight
            // A count, for the reason `FilterPills` gives: a row changes its number in place.
            model: axisItem.showing.length
            clip: true
            interactive: axisItem.showing.length > axisItem.atMostOnScreen
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: axisItem.rowHeight * 2

            delegate: FilterValueRow {
                required property int index
                readonly property var modelData: axisItem.showing[index]
                                                 ?? ({ "value": "", "label": "" })

                width: rows.width
                height: axisItem.rowHeight
                indent: axisItem.indent
                value: modelData.value
                label: modelData.label
                lit: axisItem.lit(modelData.value)
                onTriggered: axisItem.picked(modelData.value)
                // Scrolled to when the keys reach it, which also builds the row after it:
                // this view makes only the rows it draws, and the walk only finds what is
                // made — a key past the last row drawn went on to the next axis.
                onActiveFocusChanged: {
                    if (activeFocus)
                        rows.positionViewAtIndex(index, ListView.Contain)
                }
            }
        }

        Text {
            objectName: "filter-axis-none-" + axisItem.axis
            x: axisItem.indent
            width: parent.width - x - Widths.space.compact
            visible: axisItem.showing.length === 0 && axisItem.looking.trim().length > 0
            textFormat: Text.PlainText
            text: BarCaptions.noValueByThatNameLabel
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
            wrapMode: Text.Wrap
            topPadding: Widths.scale.size(4)
            bottomPadding: Widths.space.compact
        }

        Item {
            width: 1
            height: Widths.scale.size(6)
        }
    }
}
