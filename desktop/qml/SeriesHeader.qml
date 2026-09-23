// The three levels a series stacks, and what it weighs.
//
// The cover is a **fixed size**, and that is the whole arrangement: it holds the height, not
// the other way round. A series with less to say aligns its lines at the top and leaves space
// under them, so the tab bar starts at the same place for every series and for every tab —
// moving from Tomes to Description shifts nothing above it.

import QtQuick
import Leaf

Item {
    id: header

    required property string universe
    required property string work
    required property string edition
    required property string cover
    required property string makers
    required property string weights
    required property var genres
    required property string editionsLabel

    signal universeAsked()
    signal editionsAsked()

    objectName: "series-header"
    // Sized for the header with the most to say. A series with less leaves room below rather
    // than pulling the tab bar up to meet it.
    implicitHeight: 168
    height: implicitHeight

    Row {
        anchors.fill: parent
        spacing: 18

        // Flush to the left edge: a cover is what one recognises first, and it has no reason
        // to sit indented behind empty space.
        Item {
            id: art

            width: 112
            height: header.implicitHeight

            Rectangle {
                anchors.fill: parent
                radius: Theme.coverRadius
                color: Theme.onPaper
                clip: true

                Image {
                    anchors.fill: parent
                    source: header.cover
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                }
            }
        }

        Column {
            width: parent.width - art.width - parent.spacing
            spacing: 3

            Row {
                spacing: 5
                visible: header.universe.length > 0

                LevelMark {
                    level: 0
                    size: 15
                    anchors.verticalCenter: universeName.verticalCenter
                }

                LeafTextAction {
                    id: universeName

                    objectName: "series-universe"
                    label: header.universe
                    onTriggered: header.universeAsked()
                }
            }

            // The name one has in mind, and the only thing in large.
            Text {
                width: parent.width
                text: header.work
                color: Theme.ink
                font.family: Theme.displayFamily
                font.pixelSize: 28
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            // Named plainly under the work, never folded into the title.
            Row {
                spacing: 8
                visible: header.edition.length > 0 || header.editionsLabel.length > 0

                Text {
                    text: header.edition
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }

                LeafTextAction {
                    objectName: "edition-switch"
                    label: header.editionsLabel
                    visible: header.editionsLabel.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    onTriggered: header.editionsAsked()
                }
            }

            Item { width: 1; height: 8 }

            Text {
                width: parent.width
                text: header.makers
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: header.weights
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            Item { width: 1; height: 8 }

            // Genres and tags, side by side and never folded together. A plain pill and not a
            // FilterChip: that one carries an axis and a value because it goes back on the
            // wire, and nothing here is asking for anything.
            Flow {
                width: parent.width
                spacing: 5

                Repeater {
                    model: header.genres

                    Rectangle {
                        required property string modelData

                        radius: 99
                        color: Theme.onPaper
                        implicitWidth: word.implicitWidth + 18
                        implicitHeight: 21

                        Text {
                            id: word

                            anchors.centerIn: parent
                            text: parent.modelData
                            color: Theme.inkSoft
                            font.family: Theme.textFamily
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }
}
