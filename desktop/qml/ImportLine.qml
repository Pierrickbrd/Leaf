// One thing read, on the choosing step: its name, what it is, how it was read — and nothing to
// open.
//
// The queue's own card (`ImportRow`) unfolds its tree, and the tree is the checking step's
// business now: this step says what was recognised, compactly, so that forty folders are forty
// lines and not forty cards. What a card asks before the checking can start stays on its line —
// the series a lone file belongs to, a series that would leave the place it is held in, the
// reason a reading failed — and the way out is still here.

import QtQuick
import Leaf

Item {
    id: line

    required property int row
    required property string name
    required property int stage
    required property var size
    required property string reason
    required property var candidates
    required property string trouble
    required property bool folder
    required property var moves
    required property var nodes
    required property string checking
    required property int readHow
    /// How far the walk of this folder has come: `{ state, done, whole, hashing }`, the state
    /// being `Card::Counting` — none, queued, on the pool, over.
    required property var progress

    /// A walk that hashed and could not hash the members of some archive: the line says the file
    /// alone was checked, and why, in the colour of what needs a look.
    readonly property bool uncovered: line.progress.state === line.readThrough
                                      && line.progress.uncovered.length > 0

    readonly property int waitingToBeRead: 1
    readonly property int beingRead: 2
    readonly property int readThrough: 3

    /// The widths under which the line gives something up, in the order it gives it: the word
    /// in front of its count first (« 6 / 14 » says as much beside a name), then « Retirer »
    /// for a cross. They are read on the line's own width, which is its column's, and not on
    /// the window's: the column is what a narrower window takes from.
    readonly property int countBriefBelow: 470
    readonly property int removeCrossedBelow: 440
    readonly property bool brief: line.width < Widths.scale.column(line.countBriefBelow)
    readonly property bool crossed: line.width < Widths.scale.column(line.removeCrossedBelow)

    readonly property int asking: 0
    readonly property int deciding: 1
    readonly property int failed: 7

    /// What it is and what it holds, said as the card's own root said it: « Univers · 76 tomes · 8,1 Gio ».
    readonly property string kind: {
        if (line.nodes === undefined || line.nodes.length === 0)
            return line.checking.length > 0 ? line.checking : CardCaptions.stageLabel(line.stage)
        const root = line.nodes[0]
        return CardCaptions.nodeLine(root.level, root.state === undefined ? "" : root.state,
                                     root.holds === undefined ? "" : root.holds)
    }

    signal answered(string seriesId)
    signal wantsFiling(string workId, bool wanted)
    signal agreed()
    signal dropped()

    objectName: "import-row-" + name
    implicitHeight: body.height + 2 * Widths.space.row
    height: implicitHeight

    Column {
        id: body

        x: Widths.space.card
        y: Widths.space.row
        width: parent.width - 2 * Widths.space.card
        spacing: Widths.scale.room(4)

        Item {
            width: parent.width
            height: Math.max(words.height, commands.height)

            Column {
                id: words

                anchors.left: parent.left
                anchors.right: commands.left
                anchors.rightMargin: Widths.space.row
                anchors.verticalCenter: parent.verticalCenter
                spacing: Widths.scale.room(2)

                Text {
                    objectName: line.objectName + "-name"
                    width: parent.width
                    text: line.name
                    textFormat: Text.PlainText
                    color: Theme.ink
                    elide: Text.ElideRight
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(15)
                    font.weight: Font.DemiBold
                }

                Text {
                    objectName: line.objectName + "-kind"
                    width: parent.width
                    text: line.kind
                    textFormat: Text.PlainText
                    color: Theme.inkSoft
                    elide: Text.ElideRight
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(13)
                }
            }

            Row {
                id: commands

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Widths.space.row

                // How far the walk has come, kept once it is over: the checking is what holds
                // « Suivant » back, and a line that stopped counting when its tree appeared
                // left a reader looking at a greyed button with nothing saying why. Beside
                // the card's own words only while it has no tree yet: those say the same
                // count in the line under its name.
                Item {
                    id: counter

                    objectName: line.objectName + "-count"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: line.folder && line.progress.state > 0
                             && line.nodes !== undefined && line.nodes.length > 0
                    width: counted.implicitWidth
                    height: counted.implicitHeight

                    Text {
                        id: counted

                        objectName: line.objectName + "-count-words"
                        text: DropCaptions.lineCountLabel(line.progress, line.brief)
                        textFormat: Text.PlainText
                        color: line.uncovered ? Theme.alert
                                              : line.progress.state === line.waitingToBeRead ? Theme.inkSoft
                                                                                              : Theme.emerald
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.caption(12)
                    }

                    // A thin bar under the count while it moves, as wide as its words. Hung under
                    // the words and not stacked with them: in a column of its own it pushed the
                    // count three pixels up when the reading began and back down when it ended,
                    // a tremor in every line that was being walked.
                    Rectangle {
                        objectName: line.objectName + "-count-bar"
                        anchors.top: counted.bottom
                        anchors.topMargin: Widths.scale.room(3)
                        visible: line.progress.state === line.beingRead
                                 && line.progress.whole > 0
                        width: counted.implicitWidth
                        height: Widths.scale.size(3)
                        radius: height / 2
                        color: Theme.rule

                        Rectangle {
                            width: parent.width * Math.min(1, line.progress.done
                                                              / Math.max(1, line.progress.whole))
                            height: parent.height
                            radius: parent.radius
                            color: Theme.emerald
                        }
                    }
                }

                // What the whole was read from — « Complet », « Komga » or « Nu »: a kind of
                // folder, said once per line, and not where each of its values came from.
                OriginChip {
                    objectName: line.objectName + "-read"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: line.folder && line.readHow >= 0
                    origin: line.readHow < 0 ? 3 : line.readHow
                    label: DropCaptions.readHowLabel(line.readHow)
                }

                ActionButton {
                    objectName: line.objectName + "-accept"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: line.folder && line.stage === line.deciding
                    scaled: true
                    height: Widths.scale.fit(28)
                    label: DropCaptions.acceptLabel
                    onTriggered: line.agreed()
                }

                CardAction {
                    objectName: line.objectName + "-abandon"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !line.crossed
                    label: ImportCaptions.removeLabel
                    onTriggered: line.dropped()
                }

                // The same command in a column that cannot spare the word: a cross, which
                // still says « Retirer » to whoever has it read aloud.
                IconButton {
                    objectName: line.objectName + "-abandon-cross"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: line.crossed
                    glyph: "close"
                    label: ImportCaptions.removeLabel
                    side: Widths.scale.icon(16)
                    width: Widths.scale.fit(28)
                    height: width
                    activeFocusOnTab: visible
                    onTriggered: line.dropped()
                }
            }
        }

        Text {
            objectName: line.objectName + "-reason"
            width: parent.width
            visible: text.length > 0
            text: line.trouble.length > 0 ? line.trouble
                  : line.uncovered ? line.progress.uncovered : line.reason
            textFormat: Text.PlainText
            color: line.trouble.length > 0 || line.uncovered ? Theme.alert : Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            wrapMode: Text.Wrap
        }

        // A lone file asks which series it belongs to: one of those the server named, or the
        // honest sentence when it named none.
        Column {
            objectName: line.objectName + "-question"
            visible: line.stage === line.deciding && !line.folder
            width: parent.width
            spacing: Widths.scale.room(6)

            Repeater {
                model: line.stage === line.deciding ? line.candidates : []

                delegate: LeafTextAction {
                    required property var modelData

                    scaled: true
                    objectName: line.objectName + "-choose-" + modelData.seriesId
                    activeFocusOnTab: true
                    label: modelData.name
                    onTriggered: line.answered(modelData.seriesId)
                }
            }

            Text {
                objectName: line.objectName + "-nowhere"
                width: parent.width
                visible: line.candidates === undefined || line.candidates.length === 0
                textFormat: Text.PlainText
                text: DropCaptions.noSeriesLabel
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                wrapMode: Text.Wrap
            }
        }

        // A series that would leave the place it is held in: a decision, so it stays on the
        // line. Clear by default — dropping a universe is not a request to rearrange a library.
        Repeater {
            model: line.moves === undefined ? [] : line.moves

            delegate: LeafCheck {
                required property var modelData

                objectName: line.objectName + "-file-" + modelData.workId
                activeFocusOnTab: true
                width: body.width
                label: CardCaptions.alreadyElsewhereLabel(modelData.name, modelData.from)
                checked: modelData.filing
                onToggled: wanted => line.wantsFiling(modelData.workId, wanted)
            }
        }
    }
}
