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
    /// What the menu at the end of the title commands.
    required property string seriesId
    /// The other editions of the same work, for the menu the pill opens.
    required property var editions
    required property string edition
    required property string cover
    required property string makers
    required property string weights
    required property var genres
    required property string editionsLabel

    signal universeAsked()
    signal editionChosen(string seriesId)
    signal reimportAsked()

    objectName: "series-header"
    // Sized for the header with the most to say. A series with less leaves room below rather
    // than pulling the tab bar up to meet it.
    implicitHeight: 208
    height: implicitHeight

    Row {
        anchors.fill: parent
        spacing: 22

        // Flush to the left edge: a cover is what one recognises first, and it has no reason
        // to sit indented behind empty space.
        Item {
            id: art

            width: 138
            height: header.implicitHeight

            CoverShadow {
                under: header.seriesId
            }

            RoundedCover {
                anchors.fill: parent
                source: header.cover
            }

            CoverSkin { }
        }

        Column {
            width: parent.width - art.width - parent.spacing
            spacing: 3

            // The icon is part of the link, and so is the rule under it: the design draws
            // both inside one `.univers`, and a globe left outside the target was a piece of
            // the link one could not press.
            Item {
                id: universeName

                objectName: "series-universe"
                width: mark.width + 6 + name.implicitWidth
                height: name.implicitHeight + 5
                visible: header.universe.length > 0

                HoverHandler {
                    id: pointer

                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    onTapped: header.universeAsked()
                }

                Accessible.role: Accessible.Link
                Accessible.name: header.universe
                Accessible.onPressAction: header.universeAsked()

                LevelMark {
                    id: mark

                    level: 0
                    size: 17
                    anchors.verticalCenter: name.verticalCenter
                }

                Text {
                    id: name

                    anchors.left: mark.right
                    anchors.leftMargin: 6
                    text: header.universe
                    color: pointer.hovered ? Theme.ink : Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 14
                }

                // Under the words *and* under the icon: it is how a link says it is one
                // without borrowing the emerald a command wears.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: pointer.hovered ? Theme.inkSoft : Theme.rule
                }
            }

            // The name one has in mind, and the only thing in large. The three dots sit at
            // the end of it and are drawn always, not at the hover: there is one object on
            // this screen and one menu, which is not a wall of them.
            Item {
                width: parent.width
                height: title.implicitHeight

                Text {
                    id: title

                    anchors.left: parent.left
                    anchors.right: commands.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: header.work
                    color: Theme.ink
                    font.family: Theme.displayFamily
                    font.pixelSize: 36
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }

                CommandMenu {
                    id: commands

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: header.seriesId.length > 0
                    seriesId: header.seriesId
                    label: header.work
                    onReimportAsked: header.reimportAsked()
                }
            }

            // Named plainly under the work, never folded into the title.
            Row {
                spacing: 8
                visible: header.edition.length > 0 || header.editionsLabel.length > 0

                Text {
                    text: header.edition
                    color: Theme.ink
                    font.family: Theme.textFamily
                    font.pixelSize: 15
                    font.weight: Font.Medium
                    anchors.verticalCenter: parent.verticalCenter
                }

                EditionSwitch {
                    editions: header.editions
                    label: header.editionsLabel
                    anchors.verticalCenter: parent.verticalCenter
                    onChosen: seriesId => header.editionChosen(seriesId)
                }
            }

            Item { width: 1; height: 8 }

            Text {
                width: parent.width
                text: header.makers
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: 14
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: header.weights
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: 14
                elide: Text.ElideRight
            }

            Item { width: 1; height: 8 }

            // Here **and** at the foot of the description, which is where the design draws
            // them twice: up here they say at a glance what kind of book this is, down there
            // they close the civil status they belong to. A plain pill and not a
            // `FilterChip` — that one carries an axis and a value because it goes back on
            // the wire, and nothing here is asking for anything.
            Flow {
                width: parent.width
                spacing: 6

                Repeater {
                    model: header.genres

                    Rectangle {
                        required property string modelData

                        radius: 99
                        color: Theme.onPaper
                        implicitWidth: word.implicitWidth + 22
                        implicitHeight: 25

                        Text {
                            id: word

                            anchors.centerIn: parent
                            text: parent.modelData
                            color: Theme.inkSoft
                            font.family: Theme.textFamily
                            font.pixelSize: 12
                        }
                    }
                }
            }

        }
    }
}
