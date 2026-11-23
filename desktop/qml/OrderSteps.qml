// The steps of the order being written, as a table: Étape, Unité, De, À and the ×, a line per
// step, and under them the two ways to add one — a work of the drop, or another order.
//
// The columns are laid out here and read by every line, so the header and the lines cannot
// drift apart: each line placing its own boxes put « De » a few pixels off the numbers under
// it as soon as a label was long enough to push them.
//
// The lines are drawn from the steps by **count**, as `ReviewRail`'s are, each one reading its
// own entry: a model that is the list itself rebuilt every line on each change, and the line
// holding the keys was destroyed under them as it was moved.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Item {
    id: table

    readonly property var words: SheetCaptions.orderWords
    readonly property var steps: table.visible ? OrderEditor.steps : []
    readonly property var works: table.visible ? OrderEditor.works : []
    readonly property var targets: table.visible ? OrderEditor.targets : []
    readonly property int count: table.steps.length

    readonly property int handleWidth: Widths.scale.size(22)
    // « Œuvre entière » and its chevron, whole: at 120 it read « Œuvre enti… ».
    readonly property int unitWidth: Widths.scale.column(150)
    readonly property int numberWidth: Widths.scale.column(56)
    readonly property int removeWidth: Widths.scale.size(34)
    readonly property int toX: column.width - table.removeWidth - table.numberWidth
    readonly property int fromX: table.toX - Widths.space.tight - table.numberWidth
    readonly property int unitX: table.fromX - Widths.space.label - table.unitWidth

    /// The keys to the line `at`, once the lines have been redrawn by the change that moved it;
    /// to the way to add one when there is none left, and not to the line just removed.
    function focusRow(at: int): void {
        Qt.callLater(() => {
            const line = lines.itemAt(at)
            if (line)
                line.forceActiveFocus(Qt.TabFocusReason)
            else if (table.count === 0)
                addWork.takeKeys()
        })
    }

    /// The line `from`, let go at the height `sceneY`, put after every other line whose middle
    /// it was let go under. Its own line is not asked where it is: carried, it stands where the
    /// pointer is, and was found there every time.
    function dropped(from: int, sceneY: real): void {
        let to = 0
        for (let at = 0; at < lines.count; ++at) {
            const line = lines.itemAt(at)
            if (at === from || !line)
                continue
            if (sceneY > line.mapToItem(null, 0, line.height / 2).y)
                ++to
        }
        if (to !== from) {
            OrderEditor.move(from, to)
            table.focusRow(to)
        }
    }

    Item {
        id: header

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Widths.space.card
        height: Widths.scale.size(28)

        Repeater {
            model: [
                { text: table.words.stepColumn, x: table.handleWidth, width: table.unitX },
                { text: table.words.unitColumn, x: table.unitX, width: table.unitWidth },
                { text: table.words.fromColumn, x: table.fromX, width: table.numberWidth },
                { text: table.words.toColumn, x: table.toX, width: table.numberWidth }
            ]

            delegate: Text {
                required property var modelData

                x: modelData.x
                width: modelData.width
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: modelData.x >= table.fromX ? Text.AlignHCenter
                                                                : Text.AlignLeft
                text: modelData.text
                textFormat: Text.PlainText
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(11)
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: Widths.scale.letters(0.6)
            }
        }
    }

    Flickable {
        id: flick

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: Widths.space.card
        anchors.rightMargin: Widths.space.card
        contentHeight: column.height + Widths.space.card
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: column

            width: flick.width
            spacing: Widths.space.tight

            Repeater {
                id: lines

                model: table.count

                // `index` is the line's own required property, which the repeater fills.
                delegate: OrderStepRow {
                    width: column.width
                    table: table
                    // As `ReviewRail`'s line: read once more past the end of a list that shrank.
                    step: table.steps[index] ?? ({ kind: "work", label: "" })
                }
            }

            Row {
                spacing: Widths.space.row
                topPadding: Widths.space.compact

                LeafSelect {
                    id: addWork

                    objectName: "order-add-work"
                    width: Widths.scale.column(200)
                    captioned: false
                    name: table.words.addWork
                    placeholder: table.words.addWork
                    options: table.works.map((work, at) => ({ value: at, label: work }))
                    chosen: -1
                    onPicked: value => {
                        OrderEditor.addWork(table.works[value])
                    }
                }

                LeafSelect {
                    objectName: "order-add-include"
                    width: Widths.scale.column(220)
                    captioned: false
                    name: table.words.addInclude
                    placeholder: table.words.addInclude
                    options: table.targets.map((one, at) => ({ value: at, label: one.name,
                                                              possible: one.possible,
                                                              reason: one.reason }))
                    chosen: -1
                    onPicked: value => OrderEditor.addInclude(table.targets[value].id)
                }
            }
        }
    }

    FollowsFocus {
        flick: flick
    }
}
