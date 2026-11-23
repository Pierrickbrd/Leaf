// One step of the order being written: its handle, what it reads, its unit and its range, and
// its ×; an inclusion shows instead the steps it reads, folded under it and read-only — they
// are written in the other order, and are changed there.
//
// The line is a stop of its own, and the keys do there what the pointer does with the handle:
// Ctrl+↑ and Ctrl+↓ move it, the keys going with it, and Suppr removes it. A list reordered by
// dragging alone is a list nobody without a mouse can reorder. Enter shows it on the sheet, as a
// click does; the keys merely passing do not, or Tab through the table changed the sheet at
// every line and the step being looked at was always the last one crossed.
//
// A proposal wears « Proposé » edged in dashes, as a value nobody said; a step the server would
// refuse is tinted in the alert's wash and says why under itself.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Item {
    id: row

    required property int index
    /// `OrderEditor.steps[index]`.
    required property var step
    /// The table, which lays the columns out and knows where a drag lands.
    required property Item table

    readonly property var words: SheetCaptions.orderWords
    readonly property bool include: row.step.kind === "include"
    readonly property bool selected: row.step.selected === true
    readonly property string doubt: row.step.doubt ?? ""
    readonly property var inherited: row.step.inherited ?? []
    readonly property int unitValue: row.step.unit === "VOLUME" ? 1
                                   : row.step.unit === "CHAPTER" ? 2 : 0
    readonly property bool ranged: !row.include && row.unitValue > 0

    /// Moved by `by` places from the keys, which follow it to where it went.
    function shift(by: int): void {
        const to = row.index + by
        if (to < 0 || to >= row.table.count)
            return
        OrderEditor.move(row.index, to)
        row.table.focusRow(to)
    }

    function remove(): void {
        const at = row.index
        OrderEditor.remove(at)
        row.table.focusRow(Math.min(at, row.table.count - 1))
    }

    /// The range as the two boxes hold it, sent when one of them is left.
    function sendRange(): void {
        if (fromBox.text === (row.step.from ?? "") && toBox.text === (row.step.to ?? ""))
            return
        OrderEditor.setRange(row.index, row.step.unit, row.step.edition ?? "", fromBox.text, toBox.text)
    }

    objectName: "order-step-" + row.index
    implicitHeight: line.height + (folded.visible ? folded.height : 0)
                    + (said.visible ? said.implicitHeight + Widths.space.tight : 0)
                    + 2 * Widths.space.tight
    activeFocusOnTab: true
    // Above its neighbours while it is carried, so it is drawn over the lines it crosses.
    z: carry.active ? 2 : 0

    transform: Translate { y: carry.active ? carry.translation.y : 0 }

    Accessible.role: Accessible.ListItem
    Accessible.name: [row.step.label ?? "",
                      row.ranged ? (row.step.unitLabel ?? "") + " " + (row.step.from ?? "")
                                   + "–" + (row.step.to ?? "") : "",
                      row.step.proposed === true ? row.words.proposed : "",
                      row.step.absent === true ? row.words.absent : "",
                      row.doubt].filter(one => one.length > 0).join(", ")
    Accessible.description: row.words.stepKeys
    Accessible.selected: row.selected

    Keys.onPressed: event => {
        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Up) {
            row.shift(-1)
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Down) {
            row.shift(1)
            event.accepted = true
        } else if (event.key === Qt.Key_Delete) {
            row.remove()
            event.accepted = true
        } else if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                   || event.key === Qt.Key_Enter) {
            OrderEditor.select(row.index)
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: Widths.scale.size(10)
        color: row.doubt.length > 0 ? Theme.alertWash
             : row.selected ? Theme.emeraldWash : Theme.onPaper
        border.color: row.doubt.length > 0 ? Theme.alert
                    : row.selected ? Theme.emerald : Theme.rule
        border.width: 1

        CardLift {
            visible: carry.active
            level: CardLift.Bubble
        }
    }

    FocusRing {
        objectName: row.objectName + "-focus"
        cornerRadius: Widths.scale.size(10)
    }

    TapHandler {
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: {
            row.forceActiveFocus(Qt.MouseFocusReason)
            OrderEditor.select(row.index)
        }
    }

    Item {
        id: line

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: Widths.space.tight
        height: Math.max(Widths.scale.size(36), label.implicitHeight + Widths.space.compact)

        // Six dots, the shape every list that is dragged by a handle draws its handle in.
        Item {
            id: handle

            // The pointer's alone, and no stop of its own: as a button that did nothing when
            // pressed it was offered to a screen reader for nothing. The keys that move the
            // step are the line's, and the line says them.
            objectName: "order-step-handle-" + row.index
            x: 0
            width: row.table.handleWidth
            height: parent.height

            Grid {
                anchors.centerIn: parent
                columns: 2
                spacing: Widths.scale.size(3)

                Repeater {
                    model: 6

                    delegate: Rectangle {
                        width: Widths.scale.size(3)
                        height: width
                        radius: width / 2
                        color: grip.hovered || carry.active ? Theme.ink : Theme.inkFaint
                    }
                }
            }

            HoverHandler {
                id: grip

                cursorShape: carry.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            }

            DragHandler {
                id: carry

                /// Where the pointer was last seen while carrying: once let go, the handler
                /// no longer says.
                property real lastY: 0

                target: null
                xAxis.enabled: false
                onCentroidChanged: {
                    if (active)
                        lastY = centroid.scenePosition.y
                }
                onActiveChanged: {
                    if (active)
                        row.forceActiveFocus(Qt.MouseFocusReason)
                    else
                        row.table.dropped(row.index, lastY)
                }
            }
        }

        Text {
            id: label

            objectName: row.objectName + "-label"
            anchors.left: handle.right
            anchors.right: row.include ? (proposed.visible ? proposed.left : remove.left)
                                       : unit.left
            anchors.rightMargin: Widths.space.label
            anchors.verticalCenter: parent.verticalCenter
            text: row.step.label ?? ""
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: row.step.absent === true ? Theme.inkSoft : Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
            font.weight: row.include ? Font.Medium : Font.Normal
            font.italic: row.step.absent === true
        }

        DashedChip {
            id: proposed

            objectName: row.objectName + "-proposed"
            anchors.right: remove.left
            anchors.rightMargin: Widths.space.label
            anchors.verticalCenter: parent.verticalCenter
            visible: row.include && row.step.proposed === true
            label: row.words.proposed
        }

        LeafSelect {
            id: unit

            objectName: row.objectName + "-unit"
            x: row.table.unitX
            anchors.verticalCenter: parent.verticalCenter
            width: row.table.unitWidth
            visible: !row.include
            tabStops: visible
            captioned: false
            edged: true
            name: row.words.unitColumn + ", " + (row.step.label ?? "")
            options: row.words.units
            chosen: row.unitValue
            // Chapters name no edition: sent along, it had the change refused.
            onPicked: value => {
                const code = row.words.units[value].code
                OrderEditor.setRange(row.index, code,
                                     code === "CHAPTER" ? "" : (row.step.edition ?? ""),
                                     fromBox.text, toBox.text)
            }
        }

        LeafField {
            id: fromBox

            objectName: row.objectName + "-from"
            x: row.table.fromX
            anchors.verticalCenter: parent.verticalCenter
            width: row.table.numberWidth
            scaled: true
            edged: true
            // A whole work has no range: two empty boxes read as two values left to type.
            visible: row.ranged
            activeFocusOnTab: visible
            font.pixelSize: Widths.scale.body(13)
            horizontalAlignment: TextInput.AlignHCenter
            selectByMouse: true
            Accessible.name: row.words.fromColumn + ", " + (row.step.label ?? "")

            // As a sheet's field: the value, except while it is being typed.
            Binding on text {
                value: row.step.from ?? ""
                when: !fromBox.activeFocus
                restoreMode: Binding.RestoreNone
            }

            onEditingFinished: row.sendRange()
        }

        LeafField {
            id: toBox

            objectName: row.objectName + "-to"
            x: row.table.toX
            anchors.verticalCenter: parent.verticalCenter
            width: row.table.numberWidth
            scaled: true
            edged: true
            // A whole work has no range: two empty boxes read as two values left to type.
            visible: row.ranged
            activeFocusOnTab: visible
            font.pixelSize: Widths.scale.body(13)
            horizontalAlignment: TextInput.AlignHCenter
            selectByMouse: true
            Accessible.name: row.words.toColumn + ", " + (row.step.label ?? "")

            Binding on text {
                value: row.step.to ?? ""
                when: !toBox.activeFocus
                restoreMode: Binding.RestoreNone
            }

            onEditingFinished: row.sendRange()
        }

        IconButton {
            id: remove

            objectName: row.objectName + "-remove"
            anchors.right: parent.right
            anchors.rightMargin: Widths.space.tight
            anchors.verticalCenter: parent.verticalCenter
            width: Widths.scale.size(26)
            height: width
            side: Widths.scale.icon(16)
            glyph: "close"
            label: SheetCaptions.removeStep(row.step.label ?? "")
            activeFocusOnTab: true
            onTriggered: row.remove()
        }
    }

    // What an inclusion reads, as the other order writes it, under the line it stands for.
    Column {
        id: folded

        anchors.left: parent.left
        anchors.leftMargin: row.table.handleWidth + Widths.space.label
        anchors.right: parent.right
        anchors.rightMargin: Widths.space.row
        anchors.top: line.bottom
        visible: row.include && row.inherited.length > 0
        spacing: Widths.space.tight
        bottomPadding: Widths.space.tight

        Text {
            objectName: row.objectName + "-note"
            width: parent.width
            text: row.step.inheritedNote ?? ""
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
        }

        Repeater {
            model: row.inherited.length

            delegate: Item {
                id: read

                required property int index
                readonly property var one: row.inherited[read.index] ?? ({ label: "", unit: "",
                                                                           from: "", to: "" })

                width: folded.width
                height: readLabel.implicitHeight

                Text {
                    id: readLabel

                    objectName: row.objectName + "-inherited-" + read.index
                    anchors.left: parent.left
                    width: row.table.unitX - folded.x - Widths.space.label
                    text: read.one.label
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                }

                Text {
                    x: row.table.unitX - folded.x
                    width: row.table.unitWidth
                    text: read.one.unit
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.inkFaint
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                }

                Text {
                    x: row.table.fromX - folded.x
                    width: row.table.numberWidth
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    text: read.one.from
                    color: Theme.inkFaint
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                }

                Text {
                    x: row.table.toX - folded.x
                    width: row.table.numberWidth
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    text: read.one.to
                    color: Theme.inkFaint
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                }
            }
        }
    }

    Text {
        id: said

        objectName: row.objectName + "-doubt"
        anchors.left: parent.left
        anchors.leftMargin: row.table.handleWidth
        anchors.right: parent.right
        anchors.rightMargin: Widths.space.row
        anchors.top: folded.visible ? folded.bottom : line.bottom
        visible: row.doubt.length > 0
        text: row.doubt
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        color: Theme.alert
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
    }
}
