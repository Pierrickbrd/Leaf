// One file on its way in: where it is, what it is waiting for, and what you can do to it.
//
// The row is the whole interface of the queue. Everything the model knows about one file
// shows here — its stage, how far it has got, the question it is asking, the seconds until
// it tries again — because a dialog that only draws a progress bar leaves a reader guessing
// at the six other things that can be true at once.

import QtQuick
import Leaf

Item {
    id: line

    required property int row
    required property string name
    required property int stage
    required property var sent
    required property var size
    required property string reason
    required property int confidence
    required property var candidates
    required property var concerns
    required property string chosen
    required property string trouble
    required property int retryIn
    required property bool folder
    required property var moves
    required property var nodes
    required property string checking

    /// Mirrors `Imports::Stage`, which is the one place these are declared. Repeated here
    /// as numbers because QML sees an enum across a model role as an int, and naming them
    /// once at the top beats six magic numbers scattered through the bindings.
    readonly property int asking: 0
    readonly property int deciding: 1
    readonly property int ready: 2
    readonly property int sending: 3
    readonly property int filing: 4
    // `filedStage` and not `filed`, which is also a signal here: one name for both is a
    // clash, and the next reader should not have to find out which one wins.
    readonly property int filedStage: 5
    readonly property int paused: 6
    readonly property int failed: 7

    readonly property bool moving: stage === sending || stage === filing
    readonly property bool done: stage === filedStage || stage === failed

    signal answered(string seriesId)
    signal wantsFiling(string workId, bool wanted)
    signal agreed()
    signal held()
    signal released()
    signal dropped()
    signal unfolded(string at)
    signal replaced(string at, bool wanted)
    signal redeclared(string path, bool wanted)

    objectName: "import-row-" + name
    width: parent ? parent.width : 0
    implicitHeight: body.height + 40
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        radius: Theme.cardRadius
        color: Theme.surface
        border.color: Theme.rule
        border.width: 1
        antialiasing: true
    }

    Column {
        id: body

        x: 16
        y: 20
        width: parent.width - 32
        spacing: 6

        // The stage on the left, every command on the right, on one line.
        //
        // The card used to carry its name here and the tree repeated it underneath, and
        // `Accepter` sat alone at the very bottom, under whatever the tree happened to be —
        // a right edge with nothing to its left, and a hole between the two. The card **is**
        // its root node, so the name is the tree's first line and this row is what is left:
        // where it has got to, and what can be done about it.
        Item {
            width: parent.width
            height: Math.max(badge.height, commands.height)

            // Tinted, because the stage is the one thing on the card that changes on its
            // own: a reader glancing back wants to find it without reading it.
            Rectangle {
                id: badge

                objectName: line.objectName + "-stage-badge"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                visible: stageWord.text.length > 0
                height: 26
                width: stageWord.implicitWidth + 22
                radius: height / 2
                color: line.stage === line.failed ? Theme.alertWash
                       : line.moving ? Theme.emeraldWash
                                     : Theme.onPaper
                border.color: line.stage === line.failed ? Theme.alert
                              : line.moving ? Theme.emerald
                                            : Theme.rule
                border.width: 1
                antialiasing: true

                Text {
                    id: stageWord

                    objectName: line.objectName + "-stage"
                    anchors.centerIn: parent
                    text: {
                        if (line.checking.length > 0)
                            return line.checking
                        if (line.retryIn > 0)
                            return CardCaptions.tryingAgainIn(line.retryIn)
                        if (line.moving && line.size > 0)
                            return CardCaptions.stageAnd(line.stage, line.sent, line.size)
                        return CardCaptions.stageLabel(line.stage)
                    }
                    color: line.stage === line.failed ? Theme.alert
                           : line.moving ? Theme.emerald
                                         : Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
            }

            Row {
                id: commands

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                // The one thing worth doing on this card, so it takes the emerald pill the
                // rest of the application gives that role — and it sits with the other two
                // rather than alone at the bottom.
                ActionButton {
                    objectName: line.objectName + "-accept"
                    visible: line.folder && line.stage === line.deciding
                    height: 28
                    label: ImportCaptions.acceptLabel
                    onTriggered: line.agreed()
                }

                // Yields the one slot rather than stopping: the next file goes ahead and
                // this one starts again by itself when that one is done.
                //
                // Only once something is actually moving. A card that has been announced and
                // is waiting for « Importer » offered « Pause » over a queue nobody had
                // started — a button for undoing something that had not happened, which is
                // how a reader learns that the buttons mean nothing in particular.
                CardAction {
                    objectName: line.objectName + "-pause"
                    visible: line.moving || (line.stage === line.ready && Imports.busy)
                    label: ImportCaptions.pauseLabel
                    onTriggered: line.held()
                }

                CardAction {
                    objectName: line.objectName + "-resume"
                    visible: line.stage === line.paused
                    label: ImportCaptions.resumeLabel
                    onTriggered: line.released()
                }

                // A different word because it is a different decision: the server throws
                // away what it was keeping.
                CardAction {
                    objectName: line.objectName + "-abandon"
                    visible: !line.done
                    label: ImportCaptions.abandonLabel
                    alarming: true
                    onTriggered: line.dropped()
                }
            }
        }

        // What the server said, and what it could not make sense of. Never blocking — the
        // one moment somebody is looking straight at the file and can still say no.
        Text {
            objectName: line.objectName + "-reason"
            width: parent.width
            visible: text.length > 0
            text: line.trouble.length > 0 ? line.trouble : line.reason
            color: line.trouble.length > 0 ? Theme.alert : Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }

        Repeater {
            model: line.concerns === undefined ? [] : line.concerns

            delegate: Text {
                required property string modelData

                width: body.width
                text: CardCaptions.concern(modelData)
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
        }

        Rectangle {
            objectName: line.objectName + "-progress"
            visible: line.moving && line.size > 0
            width: parent.width
            height: 4
            radius: 2
            color: Theme.onPaper

            Rectangle {
                width: parent.width * Math.min(1, line.size > 0 ? line.sent / line.size : 0)
                height: parent.height
                radius: parent.radius
                color: Theme.emerald
            }
        }

        // The question, when there is one. A series to pick among those the server named;
        // and when it named none, the honest sentence rather than a search that would
        // pretend a single file can create a series.
        Column {
            objectName: line.objectName + "-question"
            visible: line.stage === line.deciding && !line.folder
            width: parent.width
            spacing: 6

            Repeater {
                model: line.stage === line.deciding ? line.candidates : []

                delegate: LeafTextAction {
                    required property var modelData

                    objectName: line.objectName + "-choose-" + modelData.seriesId
                    label: modelData.name
                    onTriggered: line.answered(modelData.seriesId)
                }
            }

            Text {
                objectName: line.objectName + "-nowhere"
                width: parent.width
                visible: line.candidates === undefined || line.candidates.length === 0
                text: ImportCaptions.noSeriesLabel
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
        }

        // The tree. One line per unfolded node, and the model decides which — the card does
        // not know what it holds, it knows how to draw it.
        Column {
            objectName: line.objectName + "-tree"
            visible: line.folder && line.nodes !== undefined && line.nodes.length > 0
            width: parent.width
            spacing: 0

            Repeater {
                model: line.nodes === undefined ? [] : line.nodes

                delegate: ImportNode {
                    required property var modelData

                    objectName: line.objectName + "-node-"
                                + (modelData.at === "" ? "root" : modelData.at)
                    node: modelData
                    onFolded: line.unfolded(modelData.at)
                    onReplacing: wanted => line.replaced(modelData.at, wanted)
                    onDeclaring: (path, wanted) => line.redeclared(path, wanted)
                }
            }
        }

        // ——— What is already here, and what agreeing would do to it ——————————
        //
        // A box per line and never one for the lot: two series in the same universe folder
        // are two decisions, and one of them may be exactly where its reader put it. They
        // start clear — dropping a universe is not a request to rearrange a library, and a
        // series quietly leaving the place somebody chose is the surprise nothing afterwards
        // would explain.

        Repeater {
            model: line.moves === undefined ? [] : line.moves

            delegate: LeafCheck {
                required property var modelData

                objectName: line.objectName + "-file-" + modelData.workId
                visible: !line.moving && !line.done
                width: body.width
                label: CardCaptions.alreadyElsewhereLabel(modelData.name, modelData.from)
                checked: modelData.filing
                onToggled: wanted => line.wantsFiling(modelData.workId, wanted)
            }
        }

    }
}
