// The arcs and sagas of a series, on its sheet: how many there are, and the button that opens
// the window where they are written.
//
// A window and not boxes among the other fields: an arc is a range with a name, a unit and
// maybe a saga above it, five values that only mean something together, and a list of them
// typed into one box was JSON. They belong to the series — an arc runs across volumes and an
// edition holds some of it — so the window is reached from the series' sheet and from no other.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Leaf

Column {
    id: block

    /// `Review.sheet`.
    required property var sheet

    readonly property var arcs: block.sheet.arcs ?? []
    readonly property var words: SheetCaptions.sheetWords
    readonly property bool editable: block.sheet.arcsEditable === true

    objectName: "review-arcs"
    visible: block.sheet.arcs !== undefined
    spacing: Widths.space.compact
    topPadding: Widths.space.row

    Rectangle {
        width: block.width
        height: 1
        color: Theme.rule
    }

    Row {
        spacing: Widths.space.row

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: block.words.arcsSection
            textFormat: Text.PlainText
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
            font.weight: Font.Medium
        }

        // A button with a height of its own, which the keys reach: a `Pill` takes its height
        // from whoever lays it out, and here nobody did — a click fell through it.
        QuietButton {
            objectName: "review-arcs-edit"
            anchors.verticalCenter: parent.verticalCenter
            scaled: true
            label: SheetCaptions.arcsEditLabel(block.arcs.length)
            enabled: block.editable
            onTriggered: window.open()
        }
    }

    ReviewArcsWindow {
        id: window

        arcs: block.arcs
        editions: block.sheet.arcsEditions ?? []
        sheetId: block.sheet.id ?? ""
    }
}
