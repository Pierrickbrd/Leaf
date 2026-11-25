// The readings of a folder whose role was read from its shape, on its sheet: each one a choice
// with what it makes of the drop, the one in force marked; then why the first, clue by clue,
// ✓ for it and ✕ against; then the files a confirmed reading writes, counted by name.
//
// A choice offered without its counts is a choice made blind, and one without its reasons is a
// guess nobody can check: « Une série, deux éditions » means nothing until « Perfect et
// Original sont des mots d'édition connus » says why. And « Ce qui sera écrit » is what makes
// the reading last — written, the shape of the disk no longer has to be guessed.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Column {
    id: block

    /// `Review.sheet`.
    required property var sheet
    /// The frame is a look at what already went: the readings are shown, not chosen.
    property bool readOnly: false

    readonly property var readings: block.sheet.readings ?? []
    readonly property var clues: block.sheet.clues ?? []
    readonly property var writes: block.sheet.writes ?? []

    /// The reading chosen from the keys, given them back once the sheet is drawn again. One
    /// that regroups the cards leaves a sheet with no readings — the node it was on is gone —
    /// and the keys were nowhere: `lost` hands them to the tree. The tree does the same for
    /// its chips.
    property int keep: -1

    /// The keys were on a reading that is no longer drawn.
    signal lost()

    function restore() {
        const chosen = choices.itemAt(block.keep)
        block.keep = -1
        if (chosen && chosen.visible)
            chosen.forceActiveFocus(Qt.OtherFocusReason)
        else
            block.lost()
    }

    objectName: "review-readings"
    visible: block.readings.length > 0 || block.clues.length > 0 || block.writes.length > 0
    spacing: Widths.space.compact

    Repeater {
        id: choices

        model: block.readings.length

        delegate: ReviewChoice {
            required property int index
            // As `ReviewRail`'s node: read once more past the end of a list that shrank.
            readonly property var reading: block.readings[index] ?? ({ index: -1, label: "",
                                                                       summary: "", badge: "",
                                                                       badgeTone: Review.Guessed,
                                                                       chosen: false })

            objectName: "review-reading-" + reading.index
            width: block.width
            label: reading.label
            detail: reading.summary
            badge: reading.badge
            badgeTone: reading.badgeTone
            chosen: reading.chosen === true
            readOnly: block.readOnly
            onPicked: {
                if (activeFocus)
                    block.keep = index
                Review.edits.chooseReading(block.sheet.id, reading.index)
                if (block.keep >= 0)
                    Qt.callLater(block.restore)
            }
        }
    }

    // ——— Why ————————————————————————————————————————————————————————————————————————

    Column {
        objectName: "review-clues"
        width: block.width
        visible: block.clues.length > 0
        topPadding: Widths.space.row

        Text {
            width: parent.width
            // The room a section's title leaves before what it heads: « Ce qui sera écrit »
            // under it has the 8 of the column, and the clues had 2, the first rule touching
            // the title.
            bottomPadding: Widths.space.heading
            text: block.sheet.cluesTitle ?? ""
            textFormat: Text.PlainText
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: Widths.scale.title(20)
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }

        Repeater {
            model: block.clues.length

            delegate: Item {
                id: clue

                required property int index
                readonly property var said: block.clues[index] ?? ({ forIt: false, text: "" })

                objectName: "review-clue-" + index
                width: parent.width
                height: words.implicitHeight + 2 * Widths.scale.fit(6)

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: Theme.rule
                }

                Text {
                    id: mark

                    anchors.left: parent.left
                    anchors.top: words.top
                    width: Widths.scale.icon(16)
                    textFormat: Text.PlainText
                    text: clue.said.forIt === true ? ReviewCaptions.clueForMark
                                                   : ReviewCaptions.clueAgainstMark
                    color: clue.said.forIt === true ? Theme.emerald : Theme.inkFaint
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                    font.weight: Font.Bold
                }

                Text {
                    id: words

                    objectName: clue.objectName + "-text"
                    anchors.left: mark.right
                    anchors.leftMargin: Widths.space.label
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: clue.said.text
                    textFormat: Text.PlainText
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                    lineHeight: 1.3
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    // ——— What will be written ———————————————————————————————————————————————————————

    Column {
        objectName: "review-writes"
        width: block.width
        visible: block.writes.length > 0
        topPadding: Widths.space.row
        spacing: Widths.space.compact

        Text {
            width: parent.width
            text: ReviewCaptions.writesLabel
            textFormat: Text.PlainText
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: Widths.scale.title(20)
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }

        Flow {
            width: parent.width
            spacing: Widths.space.pill

            Repeater {
                model: block.writes.length

                delegate: Rectangle {
                    id: pill

                    required property int index
                    readonly property var written: block.writes[index] ?? ({ text: "" })

                    objectName: "review-write-" + index
                    width: name.implicitWidth + 2 * Widths.scale.fit(8)
                    height: name.implicitHeight + Widths.scale.fit(4)
                    radius: Widths.scale.size(8)
                    color: Theme.onPaper

                    Text {
                        id: name

                        objectName: pill.objectName + "-text"
                        anchors.centerIn: parent
                        text: pill.written.text
                        textFormat: Text.PlainText
                        color: Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.caption(11)
                    }
                }
            }
        }

        // Why those files, in the quiet box every aside of the frame sits in.
        Rectangle {
            width: parent.width
            visible: (block.sheet.writesNote ?? "").length > 0
            height: note.implicitHeight + 2 * Widths.space.row
            radius: Widths.scale.size(10)
            color: Theme.onPaper
            border.color: Theme.rule
            border.width: 1

            Text {
                id: note

                objectName: "review-writes-note"
                anchors.fill: parent
                anchors.margins: Widths.space.row
                text: block.sheet.writesNote ?? ""
                textFormat: Text.PlainText
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                lineHeight: 1.3
                wrapMode: Text.WordWrap
            }
        }
    }
}
