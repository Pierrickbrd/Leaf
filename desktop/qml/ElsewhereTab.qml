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

    /// The width the blocks are laid in: the page's reading column, on the left with the
    /// tabs above. The page hands this tab the window's width, and at 1920 its tiles ran 1888
    /// pixels as a second shelf, which is what these are drawn not to be (audit, 23/10/2026).
    /// `Widths.space.column`, and not a width of its own: the files found and the settings
    /// stop at the same edge.
    readonly property int span: Math.min(width, Widths.space.column)

    objectName: "elsewhere-tab"
    // Titled sections, so a section apart, as the settings' cards are: room(16) here,
    // room(16) on the description, 28 on the settings and 8 on the search — four gaps for
    // one role (spacing audit, 23/10/2026).
    spacing: Widths.space.section

    ElsewhereBlock {
        objectName: "editions-block"
        width: tab.span
        title: SeriesCaptions.sameWorkHeading
        line: tab.page.editionsLabel
        tiles: tab.page.editions
        onOpened: seriesId => tab.editionChosen(seriesId)
    }

    ElsewhereBlock {
        objectName: "universe-block"
        width: tab.span
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
            spacing: Widths.scale.room(6)
            visible: tab.block.orders.length > 0

            Text {
                id: chosen

                objectName: "chosen-way"
                /// A stop of the page's walk — see `KeyWalk`.
                readonly property bool keyStop: true

                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                            || event.key === Qt.Key_Enter) {
                        ways.unfolded = !ways.unfolded
                        event.accepted = true
                    }
                }

                FocusRing {
                    objectName: "chosen-way-focus"
                    cornerRadius: 4
                }

                textFormat: Text.PlainText
                text: tab.block.chosenName
                color: Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.body(13)
                font.weight: Font.DemiBold

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: ways.unfolded = !ways.unfolded
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }

            Repeater {
                model: ways.unfolded ? tab.block.orders : []

                Rectangle {
                    id: way

                    required property var modelData

                    /// Chosen, and folded away again: the walk goes back to the way's name.
                    function choose() {
                        tab.block.chooseOrder(way.modelData.identifier)
                        ways.unfolded = false
                        chosen.forceActiveFocus(Qt.OtherFocusReason)
                    }

                    objectName: "way-" + modelData.identifier
                    // A name a row, and not the block's width: at 1920 a way of twelve
                    // letters lit a bar of twelve hundred pixels. Its height from its words,
                    // which a literal 34 cut at a larger desktop text size.
                    width: Math.min(ways.width, Math.max(Widths.scale.column(320),
                                                         wayName.implicitWidth + 2 * wayName.x))
                    height: Math.max(Widths.scale.size(34),
                                     wayName.implicitHeight + 2 * Widths.scale.size(8))
                    radius: Theme.cardRadius
                    color: modelData.chosen ? Theme.emeraldWash : Theme.surface

                    /// A stop of the page's walk — see `KeyWalk`.
                    readonly property bool keyStop: true

                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                                || event.key === Qt.Key_Enter) {
                            way.choose()
                            event.accepted = true
                        }
                    }

                    FocusRing {
                        objectName: way.objectName + "-focus"
                        cornerRadius: way.radius
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: way.choose()
                    }

                    Text {
                        id: wayName

                        x: Widths.scale.size(14)
                        width: parent.width - 2 * x
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: parent.modelData.name
                        color: Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(13)
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    ElsewhereBlock {
        objectName: "outside-block"
        width: tab.span
        title: SeriesCaptions.outsideHeading
        line: SeriesCaptions.outsideLine
        tiles: tab.block.outside
        onOpened: seriesId => Navigation.open(Navigation.Series, { "series": seriesId })
    }
}
