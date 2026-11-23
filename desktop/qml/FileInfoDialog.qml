// What the library knows of one volume, laid out as the reader's card lays it out: the cover
// beside the facts, the read bar under them, the summary below. Read-only, and the one way out.

import QtQuick
import QtQuick.Controls
import Leaf

LeafDialog {
    id: info

    objectName: "file-info"
    surfaceName: "file-info-surface"
    initialFocus: closeAction

    /// What `Entries.describe` answers: `lines` as `[{ name, value }]`, `cover`, `summary`
    /// and how far it has been read.
    property var about: ({})
    property string heading: ""
    readonly property var lines: info.about.lines ?? []

    Text {
        width: parent.width
        text: info.heading
        color: Theme.ink
        font.family: Theme.displayFamily
        font.pixelSize: Widths.scale.title(20)
        font.weight: Font.Bold
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
    }

    Row {
        width: parent.width
        spacing: Widths.scale.size(16)

        Item {
            id: cover

            width: Widths.scale.size(112)
            height: Widths.scale.size(168)

            RoundedCover {
                objectName: "file-info-cover"
                anchors.fill: parent
                radius: Widths.scale.size(Theme.coverRadius)
                source: info.visible && info.about.cover ? info.about.cover : ""
            }

            CoverSkin {
                radius: Widths.scale.size(Theme.coverRadius)
            }
        }

        Column {
            width: parent.width - cover.width - parent.spacing
            spacing: Widths.scale.size(7)

            Repeater {
                model: info.lines.length

                delegate: VolumeFact {
                    id: line

                    required property int index
                    readonly property var one: info.lines[index] ?? ({ "name": "", "value": "" })

                    prefix: "file-info-"
                    key: "value-" + line.index
                    name: line.one.name
                    value: line.one.value
                }
            }

            Item {
                objectName: "file-info-read-bar"
                width: parent.width
                height: visible ? 4 : 0
                visible: !!info.about.started || !!info.about.finished

                Item {
                    x: Widths.scale.size(88)
                    width: parent.width - x
                    height: 4

                    ReadBar {
                        key: "file-info"
                        shaded: false
                        started: !!info.about.started
                        finished: !!info.about.finished
                        howFarRead: info.about.howFarRead || 0
                    }
                }
            }
        }
    }

    Text {
        objectName: "file-info-summary"
        width: parent.width
        visible: text.length > 0
        text: info.about.summary || ""
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.body(14)
        wrapMode: Text.Wrap
        lineHeight: 1.2
        textFormat: Text.PlainText
    }

    commands: [
        LeafTextAction {
            id: closeAction

            objectName: "file-info-close"
            scaled: true
            label: Commands.words.closeInfo
            focus: true
            activeFocusOnTab: true
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: info.close()
        }
    ]
}
