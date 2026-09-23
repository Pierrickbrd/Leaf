// One series: a header that says who it is, and three tabs under it.
//
// **Nothing empties to refill.** The models keep what they hold until an answer arrives, so
// this file has no "clear then load" anywhere: it draws what is there, and the skeleton is
// shown at the one moment there is nothing to keep — the first opening from the shelf.
//
// The resume band sits above the tab bar, so changing tab does not move it: it belongs to the
// series and not to a tab. When nothing has been started it is not there at all — not an
// empty band saying so — and the tabs come up against the header, four lines higher.

import QtQuick
import Leaf

Item {
    id: view

    /// Replaceable so the QML can be exercised against a small model. The application never
    /// assigns it: it gets the one singleton for the whole run.
    property var page: Series
    property var volumes: Entries

    readonly property int volumesTab: 0
    readonly property int descriptionTab: 1
    readonly property int elsewhereTab: 2

    /// True when the last tab has anything to show. A work with one edition in a universe of
    /// one series has nowhere to go: two tabs then, not three with an empty one.
    readonly property bool elsewhereHasAnything:
        view.page.editions.length > 0 || Elsewhere.tiles.length > 0

    /// Asked for a re-import, of this edition or of one of its files. It travels to the
    /// window, because the import dialog belongs there and not to a row.
    signal reimportAsked(string entryId)

    /// Changing edition is not a navigation: the page and its list are re-pointed under the
    /// same header. Here and not in two places, because the switcher in the header and the
    /// last tab both do it and the day one of them learns something the other must too.
    function goToEdition(seriesId) {
        view.page.point(seriesId)
        view.volumes.point(seriesId, [])
    }

    // A tab that goes away under the reader takes the page with it rather than leaving it on
    // nothing: a series whose universe answered late is the ordinary way this happens.
    onElsewhereHasAnythingChanged: {
        if (!view.elsewhereHasAnything && view.current === view.elsewhereTab)
            view.current = view.volumesTab
    }

    objectName: "series-view"
    clip: true

    property int current: view.volumesTab

    Flickable {
        id: scroll

        anchors.fill: parent
        anchors.leftMargin: Widths.shelfMargin
        anchors.rightMargin: Widths.shelfMargin
        contentWidth: width
        contentHeight: stack.implicitHeight + 32
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: stack

            y: 16
            width: parent.width
            spacing: 14

            SeriesSkeleton {
                width: parent.width
                height: 168
                // The one screen with nothing to keep. Changing edition or opening another
                // series never shows it: there is always a page to hold on to.
                visible: !view.page.available && view.page.loading
            }

            SeriesHeader {
                width: parent.width
                visible: view.page.available
                universe: view.page.universe
                work: view.page.work
                edition: view.page.edition
                cover: view.page.cover
                makers: view.page.makers
                weights: view.page.weights
                genres: view.page.genres
                editionsLabel: view.page.editionsLabel
                seriesId: view.page.identifier
                onUniverseAsked: Shelf.filterBy({ "universe": [view.page.universe] })
                onEditionsAsked: switcher.visible = !switcher.visible
                onReimportAsked: view.reimportAsked("")
            }

            // Opens on the spot rather than taking the page away: this is a change of
            // edition, not a navigation.
            Column {
                id: switcher

                width: parent.width
                spacing: 6
                visible: false

                Repeater {
                    model: view.page.editions

                    Rectangle {
                        required property var modelData

                        width: switcher.width
                        height: 40
                        radius: Theme.cardRadius
                        color: modelData.here ? Theme.emeraldWash : Theme.surface

                        TapHandler {
                            onTapped: {
                                view.goToEdition(parent.modelData.seriesId)
                                switcher.visible = false
                            }
                        }

                        Column {
                            x: 14
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1

                            Text {
                                text: parent.parent.modelData.name
                                color: Theme.ink
                                font.family: Theme.displayFamily
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }

                            // Under the name and on both, the one being read included: it is
                            // often the only thing that really tells two editions apart, so
                            // it cannot disappear at the moment one is chosen.
                            Text {
                                text: parent.parent.modelData.detail
                                color: Theme.inkFaint
                                font.family: Theme.textFamily
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            ResumeBand {
                width: parent.width
                // Absent, not empty: a grey band announcing « nothing started » would say the
                // same thing and take the room of four volumes.
                visible: Resume.available && Resume.seriesId === view.page.identifier
            }

            SeriesTabs {
                width: parent.width
                visible: view.page.available
                current: view.current
                hasElsewhere: view.elsewhereHasAnything
                asGrid: Preferences.volumesAsGrid
                query: view.volumes.query
                onChosen: which => view.current = which
                onViewToggled: Preferences.volumesAsGrid = !Preferences.volumesAsGrid
                onSearched: text => view.volumes.searchFor(text)
            }

            // The list. The page scrolls, not a box inside it — one scroll, like the shelf,
            // and the cut is the bottom of the screen rather than a box with white under it.
            Column {
                id: asList

                width: parent.width
                spacing: 0
                visible: view.current === view.volumesTab && !Preferences.volumesAsGrid

                Repeater {
                    // Emptied rather than hidden: a repeater in an invisible column still
                    // builds every one of its delegates, and this page has two of them.
                    model: asList.visible ? view.volumes : null

                    // A row is a file, a gap, a separator or a stretch of chapters, and
                    // the four are read in one column. A loader picks the shape rather than
                    // one component drawing four things and hiding three.
                    //
                    // The three components are declared **inside** the loader: a component
                    // resolves its bindings in the scope it was written in, and one written
                    // beside the repeater cannot see the row.
                    Loader {
                        id: line

                        required property string entryId
                        required property string number
                        required property string title
                        required property string pages
                        required property int state
                        required property real howFarRead
                        required property string timesFinished
                        required property int kind
                        required property string detail
                        required property int depth
                        required property string fileName

                        width: parent.width
                        sourceComponent: kind === 2 ? arcSeparator
                                       : kind === 3 ? chapterStretch
                                                    : volumeLine

                        Component {
                            id: volumeLine

                            VolumeRow {
                                entryId: line.entryId
                                number: line.number
                                title: line.title
                                pages: line.pages
                                state: line.state
                                howFarRead: line.howFarRead
                                timesFinished: line.timesFinished
                                neverReadWord: SeriesCaptions.neverRead
                                seriesId: view.page.identifier
                                fileName: line.fileName
                                onReimportAsked: view.reimportAsked(line.entryId)
                            }
                        }

                        Component {
                            id: arcSeparator

                            ArcRow {
                                name: line.title
                                range: line.detail
                                depth: line.depth
                            }
                        }

                        Component {
                            id: chapterStretch

                            ChapterRange { range: line.detail }
                        }
                    }
                }

                Text {
                    width: parent.width
                    text: SeriesCaptions.nothingFound
                    visible: SeriesCaptions.narrowedToNothing
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 12
                    topPadding: 12
                }
            }

            // The same list, in covers. No separators here: a full-width marker would cut the
            // grid into blocks of uneven height, and the grid exists to show covers. Arcs are
            // a linear reading and they live in the list.
            Flow {
                id: asGrid

                width: parent.width
                spacing: Widths.shelfGap
                visible: view.current === view.volumesTab && Preferences.volumesAsGrid

                readonly property real side: 104

                Repeater {
                    model: asGrid.visible ? view.volumes : null

                    Loader {
                        id: cell

                        required property string entryId
                        required property string number
                        required property string title
                        required property string pages
                        required property string cover
                        required property string fileName
                        required property int state
                        required property real howFarRead
                        required property int kind

                        // A file and a gap are drawn; a separator and a stretch of chapters
                        // are not, and an invisible item is one a `Flow` steps over rather
                        // than laying out at nought by nought.
                        visible: kind <= 1
                        active: visible
                        sourceComponent: oneVolume

                        Component {
                            id: oneVolume

                            VolumeTile {
                                entryId: cell.entryId
                                number: cell.number
                                title: cell.title
                                pages: cell.pages
                                cover: cell.cover
                                fileName: cell.fileName
                                state: cell.state
                                howFarRead: cell.howFarRead
                                seriesId: view.page.identifier
                                coverWidth: asGrid.side
                                coverHeight: asGrid.side * 1.5
                                onReimportAsked: view.reimportAsked(cell.entryId)
                            }
                        }
                    }
                }
            }

            SeriesDescription {
                width: parent.width
                visible: view.current === view.descriptionTab
                summary: view.page.summary
                credits: view.page.credits
                nature: view.page.nature
                holding: view.page.holding
            }

            ElsewhereTab {
                width: parent.width
                visible: view.current === view.elsewhereTab
                page: view.page
                onEditionChosen: seriesId => view.goToEdition(seriesId)
            }
        }
    }
}
