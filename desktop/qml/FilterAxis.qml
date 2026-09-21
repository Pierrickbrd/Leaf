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

    /// Above this many, the list gets a field to search itself. Twelve is where a column
    /// stops being something you scan and starts being something you hunt through.
    readonly property int tooManyToScan: 12
    /// And above this many, it scrolls inside its own bounds rather than growing. Five rows
    /// is enough to show that there is a list and that it continues; a library with three
    /// hundred authors would otherwise make one axis nine thousand pixels tall, and build
    /// three hundred items to draw the five anybody can see.
    readonly property int atMostOnScreen: 5
    readonly property int rowHeight: 30
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

    Rectangle {
        id: head

        objectName: "filter-axis-head-" + axisItem.axis
        width: parent.width
        height: 34
        radius: 8
        color: pointer.hovered ? Theme.onPaper : "transparent"

        HoverHandler {
            id: pointer

            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            onTapped: axisItem.open = !axisItem.open
        }

        Text {
            objectName: "filter-axis-chevron-" + axisItem.axis
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            text: axisItem.open ? "⌄" : "›"
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: 15
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            objectName: "filter-axis-title-" + axisItem.axis
            anchors.left: parent.left
            anchors.leftMargin: 28
            anchors.verticalCenter: parent.verticalCenter
            text: axisItem.title
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: 14
            font.weight: axisItem.litHere > 0 ? Font.Medium : Font.Normal
        }

        // What the axis is doing, without opening it: how many of its values are lit, or
        // how many there are to choose from when none is.
        Text {
            objectName: "filter-axis-tally-" + axisItem.axis
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: axisItem.litHere > 0 ? String(axisItem.litHere) : String(axisItem.values.length)
            color: axisItem.litHere > 0 ? Theme.emerald : Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: 13
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

        LeafSearchLine {
            objectName: "filter-axis-search-" + axisItem.axis
            x: 8
            width: parent.width - 16
            visible: axisItem.values.length > axisItem.tooManyToScan
            placeholder: axisItem.title
            onAsked: text => axisItem.looking = text
        }

        // A view and not a repeater: it builds the rows it draws and no more. The axis is
        // as tall as its values up to seven of them, and scrolls beyond that.
        ListView {
            id: rows

            objectName: "filter-axis-rows-" + axisItem.axis
            width: body.width
            height: Math.min(axisItem.showing.length, axisItem.atMostOnScreen)
                    * axisItem.rowHeight
            model: axisItem.showing
            clip: true
            interactive: axisItem.showing.length > axisItem.atMostOnScreen
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: axisItem.rowHeight * 2

            delegate: FilterValueRow {
                required property var modelData

                width: rows.width
                height: axisItem.rowHeight
                value: modelData.value
                label: modelData.label
                lit: axisItem.lit(modelData.value)
                onTriggered: axisItem.picked(modelData.value)
            }
        }

        Text {
            objectName: "filter-axis-none-" + axisItem.axis
            x: 28
            width: parent.width - 36
            visible: axisItem.showing.length === 0 && axisItem.looking.trim().length > 0
            text: Search.noValueByThatNameLabel
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 13
            wrapMode: Text.Wrap
            topPadding: 4
            bottomPadding: 8
        }

        Item {
            width: 1
            height: 6
        }
    }
}
