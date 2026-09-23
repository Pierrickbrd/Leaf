// The last tab: where to go from here that is not here.
//
// Two blocks, titled apart and never in the same list: changing edition is the same book in
// another binding, walking the universe is other books. Under one tab because one comes here
// for the same reason — leaving this page — and a block with nothing in it is not drawn at
// all, the tab itself going with the last of them.
//
// **When the universe declares a way through, its block becomes that way**: a tile per step
// and not per series, because a step is a stretch of one work and the same work may come
// round again. What the way does not name comes after, under a heading of its own, rather
// than slipped onto the end where it would read as more of the walk.

import QtQuick
import Leaf

Column {
    id: tab

    /// Replaceable so the QML can be exercised against a small model, like the page itself.
    property var page: Series
    property var block: Elsewhere

    /// Chosen in the editions block: the same book, so the page changes under the header
    /// rather than going anywhere.
    signal editionChosen(string seriesId)

    objectName: "elsewhere-tab"
    spacing: 22

    ElsewhereBlock {
        objectName: "editions-block"
        width: parent.width
        title: SeriesCaptions.sameWorkHeading
        line: tab.page.editionsLabel
        tiles: tab.page.editions
        onOpened: seriesId => tab.editionChosen(seriesId)
    }

    ElsewhereBlock {
        objectName: "universe-block"
        width: parent.width
        title: SeriesCaptions.universeHeading
        line: SeriesCaptions.universeLine
        tiles: tab.block.tiles
        onOpened: seriesId => Navigation.open(Navigation.Series, { "series": seriesId })

        // The way being walked, and the others under it when asked. The same mechanic as the
        // edition switcher in the header, which is already learned: a second kind of chooser
        // on one page would be a second one to learn for nothing.
        Column {
            id: ways

            property bool unfolded: false

            width: parent.width
            spacing: 6
            visible: tab.block.orders.length > 0

            Text {
                objectName: "chosen-way"
                text: tab.block.chosenName
                color: Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold

                TapHandler { onTapped: ways.unfolded = !ways.unfolded }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }

            Repeater {
                model: ways.unfolded ? tab.block.orders : []

                Rectangle {
                    required property var modelData

                    objectName: "way-" + modelData.identifier
                    width: ways.width
                    height: 34
                    radius: Theme.cardRadius
                    color: modelData.chosen ? Theme.emeraldWash : Theme.surface

                    TapHandler {
                        onTapped: {
                            tab.block.chooseOrder(parent.modelData.identifier)
                            ways.unfolded = false
                        }
                    }

                    Text {
                        x: 14
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData.name
                        color: Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: 13
                    }
                }
            }
        }
    }

    ElsewhereBlock {
        objectName: "outside-block"
        width: parent.width
        title: SeriesCaptions.outsideHeading
        line: SeriesCaptions.outsideLine
        tiles: tab.block.outside
        onOpened: seriesId => Navigation.open(Navigation.Series, { "series": seriesId })
    }
}
