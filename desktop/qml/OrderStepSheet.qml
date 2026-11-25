// The sheet of the step selected, on the right of the order editor.
//
// An inclusion says what it is and why it is there — its steps read in its place and kept up
// to date, nothing written twice — the order it reads, chosen from those it may read, how many
// steps it brings and what the order comes to once flattened; and, always, that a loop is
// refused, with the orders it would refuse named. A step of a work says its work, whether the
// drop holds it, and the edition its volumes are counted in when the work has several.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Item {
    id: sheet

    readonly property var words: SheetCaptions.orderWords
    readonly property var step: sheet.visible ? OrderEditor.step : ({})
    readonly property var steps: sheet.visible ? OrderEditor.steps : []
    readonly property var tabs: sheet.visible ? OrderEditor.tabs : []
    readonly property int at: {
        for (let one = 0; one < sheet.steps.length; ++one) {
            if (sheet.steps[one].selected === true)
                return one
        }
        return -1
    }
    readonly property var line: sheet.at >= 0 ? sheet.steps[sheet.at] : ({})
    readonly property bool include: sheet.step.kind === "include"
    readonly property var targets: sheet.step.targets ?? []
    readonly property var chosen: sheet.tabs.find(tab => tab.chosen === true) ?? ({})
    readonly property string chosenName: sheet.chosen.name ?? ""
    readonly property string targetName: {
        for (const one of sheet.targets) {
            if (one.id === sheet.step.target)
                return one.name
        }
        return sheet.step.target ?? ""
    }
    /// Why each order it may not read is refused, the order itself left out: that one is the
    /// rule said just above, and naming it again said nothing. By its id: two orders may bear
    /// one name, and the other one's reason went unsaid.
    readonly property var loops: sheet.targets.filter(one => !one.possible
                                                      && one.id !== sheet.chosen.id)
    readonly property var editions: sheet.step.editions ?? []

    clip: true

    Flickable {
        id: flick

        anchors.fill: parent
        anchors.margins: Widths.space.card
        contentHeight: column.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        visible: sheet.at >= 0

        Column {
            id: column

            width: flick.width
            spacing: Widths.space.row

            Item {
                width: column.width
                height: Math.max(title.implicitHeight + kind.implicitHeight, chip.height)

                Text {
                    id: title

                    objectName: "order-sheet-title"
                    anchors.left: parent.left
                    anchors.right: chip.left
                    anchors.rightMargin: Widths.space.label
                    text: SheetCaptions.stepNumbered(sheet.at + 1)
                    textFormat: Text.PlainText
                    color: Theme.ink
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.title(17)
                    font.weight: Font.DemiBold
                }

                Text {
                    id: kind

                    anchors.left: title.left
                    anchors.right: title.right
                    anchors.top: title.bottom
                    text: sheet.include ? sheet.words.includeAnother : (sheet.line.label ?? "")
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                }

                DashedChip {
                    id: chip

                    anchors.right: parent.right
                    anchors.top: parent.top
                    visible: sheet.include && sheet.line.proposed === true
                    label: sheet.words.proposed
                }
            }

            // ——— An inclusion ———————————————————————————————————————————————————————

            Rectangle {
                width: column.width
                visible: sheet.include
                height: explained.implicitHeight + 2 * Widths.space.row
                radius: Widths.scale.size(10)
                color: Theme.emeraldWash
                border.color: Theme.emerald
                border.width: 1

                Column {
                    id: explained

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Widths.space.row
                    spacing: Widths.space.tight

                    Text {
                        width: parent.width
                        text: sheet.words.includeInstead
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        color: Theme.emerald
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(13)
                        font.weight: Font.DemiBold
                    }

                    Text {
                        width: parent.width
                        text: sheet.words.includeExplained
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        color: Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.caption(12)
                        lineHeight: 1.3
                    }
                }
            }

            LeafSelect {
                objectName: "order-target"
                width: column.width
                visible: sheet.include
                tabStops: visible
                name: sheet.words.includedOrder
                // An inclusion that names no order it can read showed an empty frame.
                placeholder: sheet.words.chooseIncludedOrder
                options: sheet.targets.map((one, at) => ({ value: at, label: one.name,
                                                          possible: one.possible,
                                                          reason: one.reason }))
                chosen: sheet.targets.findIndex(one => one.id === sheet.step.target)
                onPicked: value => OrderEditor.setInclude(sheet.at, sheet.targets[value].id)
            }

            CardAction {
                objectName: "order-sheet-keep"
                visible: sheet.include && sheet.line.proposed === true
                activeFocusOnTab: visible
                label: sheet.words.keepInclusion
                onTriggered: OrderEditor.acceptProposal(sheet.at)
            }

            Repeater {
                model: sheet.include
                       ? [{ name: "order-sheet-inherited", label: sheet.words.inheritedSteps,
                            value: String(sheet.step.inheritedCount ?? 0) },
                          { name: "order-sheet-flat", label: sheet.words.flatOrder,
                            value: SheetCaptions.flatSteps(sheet.step.flatCount ?? 0) }]
                       : []

                delegate: Item {
                    id: fact

                    required property var modelData

                    width: column.width
                    height: Math.max(factLabel.implicitHeight, factValue.implicitHeight)

                    Accessible.role: Accessible.StaticText
                    Accessible.name: fact.modelData.label + ", " + fact.modelData.value

                    Text {
                        id: factLabel

                        anchors.left: parent.left
                        text: fact.modelData.label
                        textFormat: Text.PlainText
                        color: Theme.inkSoft
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.caption(12)
                    }

                    Text {
                        id: factValue

                        objectName: fact.modelData.name
                        anchors.right: parent.right
                        text: fact.modelData.value
                        textFormat: Text.PlainText
                        color: Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(13)
                        font.weight: Font.Medium
                    }
                }
            }

            Text {
                width: column.width
                visible: sheet.include
                text: SheetCaptions.followsIt(sheet.targetName, sheet.chosenName) + "\n"
                      + sheet.words.nothingCopied
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                lineHeight: 1.3
            }

            Rectangle {
                objectName: "order-sheet-loop"
                width: column.width
                visible: sheet.include
                height: loopText.implicitHeight + 2 * Widths.space.row
                radius: Widths.scale.size(10)
                color: sheet.loops.length > 0 ? Theme.alertWash : Theme.onPaper
                border.color: sheet.loops.length > 0 ? Theme.alert : Theme.rule
                border.width: 1

                Accessible.role: Accessible.StaticText
                Accessible.name: [sheet.words.loopRefused, sheet.words.loopExplained]
                                 .concat(sheet.loops.map(one => one.reason)).join(" ")

                Column {
                    id: loopText

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Widths.space.row
                    spacing: Widths.space.tight

                    Text {
                        width: parent.width
                        text: sheet.words.loopRefused
                        textFormat: Text.PlainText
                        color: sheet.loops.length > 0 ? Theme.alert : Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(13)
                        font.weight: Font.DemiBold
                    }

                    Text {
                        width: parent.width
                        text: sheet.words.loopExplained
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        color: Theme.inkSoft
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.caption(12)
                    }

                    Repeater {
                        model: sheet.loops

                        delegate: Text {
                            required property var modelData

                            width: loopText.width
                            text: modelData.reason
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            color: Theme.alert
                            font.family: Theme.textFamily
                            font.pixelSize: Widths.scale.caption(12)
                        }
                    }
                }
            }

            // ——— A step of a work ———————————————————————————————————————————————————

            Text {
                objectName: "order-sheet-absent"
                width: column.width
                visible: !sheet.include && sheet.line.absent === true
                text: sheet.words.absent
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
            }

            LeafSelect {
                objectName: "order-sheet-edition"
                width: column.width
                visible: !sheet.include && sheet.editions.length > 0
                tabStops: visible
                name: sheet.words.edition
                // « Aucune » first: an edition once chosen could not be let go, and a range of
                // volumes counted in one never became a range of the whole.
                options: [{ value: 0, label: sheet.words.noEdition }]
                         .concat(sheet.editions.map((one, at) => ({ value: at + 1, label: one })))
                // An edition this drop does not hold is no « Aucune »: nothing is shown chosen.
                chosen: {
                    const said = sheet.line.edition ?? ""
                    const at = sheet.editions.indexOf(said)
                    return at >= 0 ? at + 1 : said === "" ? 0 : -1
                }
                onPicked: value => OrderEditor.setRange(sheet.at, sheet.line.unit ?? "",
                                                        value > 0 ? sheet.editions[value - 1] : "",
                                                        sheet.line.from ?? "",
                                                        sheet.line.to ?? "")
            }

            Text {
                objectName: "order-sheet-doubt"
                width: column.width
                visible: text.length > 0
                text: sheet.line.doubt ?? ""
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: Theme.alert
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
            }
        }
    }

    FollowsFocus {
        flick: flick
    }
}
