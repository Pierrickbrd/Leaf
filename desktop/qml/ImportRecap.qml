// The third step, « Envoyer »: what is about to travel, said once at the top and then line by
// line, in a box that scrolls under a header that does not — and the same frame once a line
// has gone, each line then carrying the button that sends it, stops it, sends it again.
//
// One frame for both moments, because the list a reader looked at before pressing anything is
// the list they watch afterwards: the cards of the queue, which this replaced, were a second
// drawing of the same things with eight states and a tree to unfold, and nothing on them could
// be read at a glance. Nothing is ticked here. The ticks that decide what replaces what are the
// checking step's, where the sentence that says what each one does stands beside it; repeated
// on this step they were boxes nobody could tell the use of, and their default is the one that
// overwrites nothing.
//
// Short windows drop the paragraphs first and keep the titles, then the list scrolls — the
// same order the first step uses, so that the two do not disagree about what matters.
//
// The three marks at the end of a line — percentage, round button, cross — stand in three
// slots of their own, always the same: the button is at one x on every line whether the cross
// shows or not, and neither it nor the percentage shifts when the cross appears.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Item {
    id: recap

    objectName: "import-recap"

    /// Mirrors `Card::Stage`, which a line draws by.
    readonly property int asking: 0
    readonly property int ready: 2
    readonly property int sending: 3
    readonly property int filing: 4
    readonly property int filed: 5
    readonly property int paused: 6
    readonly property int failed: 7

    /// Below this height the paragraph under the title is the first thing to go.
    readonly property real roomForExplanation: Widths.scale.size(420)
    /// And the sentence under the checking box goes before that one does.
    readonly property real roomForVerifyAside: Widths.scale.size(470)

    readonly property bool going: ImportTally.started
    readonly property int gap: Widths.space.row

    Rectangle {
        id: banner

        objectName: "import-recap-banner"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: bannerColumn.implicitHeight + 2 * Widths.space.card
        radius: Theme.cardRadius
        color: Theme.surface
        border.color: Theme.rule
        border.width: 1

        Column {
            id: bannerColumn

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Widths.space.card
            spacing: Widths.space.tight

            Item {
                width: parent.width
                height: Math.max(title.implicitHeight, about.implicitHeight)

                Text {
                    id: title

                    objectName: "import-recap-title"
                    anchors.left: parent.left
                    anchors.right: about.left
                    anchors.rightMargin: Widths.space.card
                    anchors.baseline: about.baseline
                    elide: Text.ElideRight
                    text: !recap.going ? RecapCaptions.recapTitle(ImportTally.toSendFiles)
                          : ImportTally.finished
                            ? RecapCaptions.sentTitle(ImportTally.sentFiles, ImportTally.sendingFiles)
                            : RecapCaptions.sendingTitle(ImportTally.sentFiles, ImportTally.sendingFiles)
                    textFormat: Text.PlainText
                    color: Theme.ink
                    font.family: Theme.displayFamily
                    font.pixelSize: Widths.scale.title(28)
                    font.weight: Font.DemiBold
                }

                Text {
                    id: about

                    objectName: "import-recap-about"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: !recap.going ? RecapCaptions.recapAbout(ImportTally.toSendBytes, Imports.count)
                          : ImportTally.finished
                            ? RecapCaptions.sentAbout(ImportTally.sentBytes, ImportTally.failedLines)
                            : RecapCaptions.sendingAbout(ImportTally.sentBytes, ImportTally.sendingBytes)
                    textFormat: Text.PlainText
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(13)
                }
            }

            Text {
                objectName: "import-recap-explains"
                width: parent.width
                // Over a send that is over, it explained buttons nothing could be done with.
                visible: text.length > 0 && recap.height >= recap.roomForExplanation
                text: RecapCaptions.bannerExplains(recap.going, ImportTally.finished)
                textFormat: Text.PlainText
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.body(13)
                lineHeight: 1.15
                wrapMode: Text.Wrap
            }

            // How far the whole of it has gone, once a line has: the numbers above say it and
            // a bar is how it is read from across the room. Its room is kept from the start
            // and only the bar comes and goes: shown, it pushed the banner taller and the list
            // under it down, a jump as the first bytes went.
            Rectangle {
                objectName: "import-recap-bar"
                width: parent.width
                height: Math.max(3, Widths.scale.size(4))
                radius: height / 2
                opacity: recap.going && ImportTally.sentBytes > 0 ? 1 : 0
                color: Theme.rule

                Rectangle {
                    objectName: "import-recap-bar-fill"
                    width: parent.width * (ImportTally.sendingBytes > 0
                                           ? Math.min(1, ImportTally.sentBytes / ImportTally.sendingBytes) : 0)
                    height: parent.height
                    radius: parent.radius
                    color: Theme.emerald
                }
            }
        }
    }

    Rectangle {
        id: box

        objectName: "import-recap-box"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: banner.bottom
        anchors.topMargin: recap.gap
        // As tall as its lines and no taller: the box that took what was left was a window of
        // emptiness under one line, with the checking box stranded at the foot of it.
        height: Math.max(header.height + 2, Math.min(
                    header.height + lines.contentHeight + 2,
                    recap.height - banner.height - recap.gap
                    - (verify.visible ? verify.height + recap.gap : 0)))
        radius: Theme.cardRadius
        color: Theme.surface
        border.color: Theme.rule
        border.width: 1
        antialiasing: true

        readonly property real pad: Widths.space.card
        readonly property real volumesColumn: Widths.scale.size(80)
        readonly property real sizeColumn: Widths.scale.size(100)
        // The three slots at the end of a line, from the right: the cross, the round button,
        // the percentage. Each has its own width whether it is drawn or not.
        readonly property real crossSlot: Widths.scale.size(26)
        readonly property real buttonSide: Widths.scale.size(40)
        readonly property real percentSlot: Widths.scale.size(38)
        readonly property real slotGap: Widths.space.row
        /// Where the round button's right edge is, counted from the box's right edge.
        readonly property real buttonInset: pad + crossSlot + slotGap
        /// The left edge of the percentage slot, counted from the box's right edge.
        readonly property real stateWidth: buttonInset + buttonSide + slotGap + percentSlot
        readonly property real sizeInset: stateWidth + slotGap
        readonly property real volumesInset: sizeInset + sizeColumn + slotGap

        // The header stays where it is while the lines under it scroll.
        Item {
            id: header

            objectName: "import-recap-header"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 1
            height: headerSeries.implicitHeight + 2 * Widths.space.row

            Text {
                id: headerSeries

                anchors.left: parent.left
                anchors.leftMargin: box.pad
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: RecapCaptions.recapSeries
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                font.weight: Font.DemiBold
            }

            Text {
                objectName: "import-recap-header-volumes"
                anchors.right: parent.right
                anchors.rightMargin: box.volumesInset
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: RecapCaptions.recapVolumes
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                font.weight: Font.DemiBold
            }

            Text {
                objectName: "import-recap-header-size"
                anchors.right: parent.right
                anchors.rightMargin: box.sizeInset
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: RecapCaptions.recapSize
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                font.weight: Font.DemiBold
            }

            // Centred over the round button, which is the column's one fixed mark.
            Text {
                objectName: "import-recap-header-state"
                x: parent.width - box.buttonInset - box.buttonSide / 2 - implicitWidth / 2
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: RecapCaptions.recapState
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                font.weight: Font.DemiBold
            }
        }

        ListView {
            id: lines

            objectName: "import-recap-lines"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.bottom: parent.bottom
            anchors.margins: 1
            clip: true
            model: Imports
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height

            delegate: Item {
                id: line

                required property int index
                required property string name
                required property int stage
                required property int volumes
                required property var bytes
                required property string about
                required property bool alarm
                required property real fraction
                required property bool released
                required property bool shown

                // Once a line was set going its cross is there to put it back: a line that
                // was let go, one stopped part-way, one that failed.
                readonly property bool started: line.released || line.fraction > 0
                                                || line.stage === recap.paused
                                                || line.stage === recap.failed
                readonly property bool cancellable: line.shown && line.stage !== recap.filed
                                                    && line.stage !== recap.asking
                                                    && line.started
                readonly property string kind: line.stage === recap.filed ? "done"
                    : line.stage === recap.failed ? "retry"
                    : (line.stage === recap.sending || line.stage === recap.filing
                       || (line.stage === recap.ready && line.released)) ? "pause" : "play"

                objectName: "import-recap-line-" + line.name
                width: lines.width
                visible: line.shown
                height: visible ? Math.max(texts.implicitHeight, box.buttonSide) + 2 * Widths.space.row
                                  + Widths.space.tight
                                : 0

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.rule
                }

                Column {
                    id: texts

                    anchors.left: parent.left
                    anchors.leftMargin: box.pad
                    anchors.right: parent.right
                    anchors.rightMargin: box.volumesInset + box.volumesColumn
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Widths.space.tight / 2

                    Text {
                        id: label

                        objectName: line.objectName + "-name"
                        width: parent.width
                        text: line.name
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        color: Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(14)
                        font.weight: Font.DemiBold
                    }

                    Text {
                        objectName: line.objectName + "-about"
                        width: parent.width
                        text: line.about
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: line.alarm ? Theme.alert : Theme.inkSoft
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.caption(12)
                    }
                }

                Text {
                    objectName: line.objectName + "-count"
                    width: box.volumesColumn
                    x: parent.width - box.volumesInset - width
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    textFormat: Text.PlainText
                    text: line.volumes
                    color: Theme.ink
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(14)
                }

                Text {
                    objectName: line.objectName + "-size"
                    width: box.sizeColumn
                    x: parent.width - box.sizeInset - width
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    textFormat: Text.PlainText
                    text: RecapCaptions.sizeOf(line.bytes)
                    color: Theme.ink
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(14)
                }

                Text {
                    objectName: line.objectName + "-percent"
                    width: box.percentSlot
                    x: parent.width - box.stateWidth
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    visible: line.stage !== recap.filed && line.fraction > 0
                    textFormat: Text.PlainText
                    text: RecapCaptions.percentOf(line.fraction)
                    color: line.alarm ? Theme.alert : Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                }

                SendMark {
                    id: go

                    objectName: line.objectName + "-go"
                    x: parent.width - box.buttonInset - width
                    anchors.verticalCenter: parent.verticalCenter
                    kind: line.kind
                    fraction: line.fraction
                    tone: line.alarm ? Theme.alert : Theme.emerald
                    waiting: line.stage === recap.asking
                    label: line.kind === "pause" ? RecapCaptions.lineStop
                           : line.kind === "retry" ? RecapCaptions.lineRetry
                           : RecapCaptions.lineStart
                    onTriggered: line.kind === "pause" ? ImportQueue.pause(line.index)
                                                       : ImportQueue.start(line.index)
                }

                // Always in its slot, and only drawn when there is something to put back.
                IconButton {
                    objectName: line.objectName + "-cancel"
                    x: parent.width - box.pad - box.crossSlot
                    anchors.verticalCenter: parent.verticalCenter
                    width: box.crossSlot
                    height: width
                    visible: line.cancellable
                    activeFocusOnTab: visible
                    glyph: "close"
                    side: Widths.scale.icon(14)
                    label: RecapCaptions.lineCancel
                    onTriggered: ImportQueue.cancel(line.index)
                }
            }
        }
    }

    // What the first step chose and this one only recalls: read-only, because the choice was
    // made before anything was read, and what was read says how it was made.
    Rectangle {
        id: verify

        objectName: "import-recap-verify"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: box.bottom
        anchors.topMargin: recap.gap
        height: verifyCheck.implicitHeight + 2 * Widths.space.card
        radius: Theme.cardRadius
        color: Theme.surface
        border.color: Theme.rule
        border.width: 1
        visible: Imports.verifying

        LeafCheck {
            id: verifyCheck

            objectName: "import-recap-verify-check"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Widths.space.card
            checked: true
            label: DropCaptions.verifyLabel
            aside: recap.height >= recap.roomForVerifyAside ? RecapCaptions.recapVerifies : ""
        }
    }
}
