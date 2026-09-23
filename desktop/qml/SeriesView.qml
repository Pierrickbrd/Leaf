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

    // Started when there is nothing to draw, stopped the moment there is. Its `triggered`
    // is what the skeleton waits on, so a fast answer draws no skeleton at all.
    Timer {
        id: waited

        property bool triggered: false

        interval: 200
        onTriggered: waited.triggered = true
    }

    readonly property bool bare: !view.page.available && view.page.loading
    onBareChanged: {
        if (view.bare) {
            waited.triggered = false
            waited.restart()
            return
        }
        waited.stop()
        waited.triggered = false
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

    // The way out, which the page carries because « a control that leaves a page belongs to
    // it » — the bar keeps the brand and drops everything that acts on a shelf. The settings
    // screen has had one since it was written; this page had nothing but Escape, which is an
    // instruction nobody can see.
    //
    // Outside the flick and not the first row of it: a control that leaves the page cannot be
    // something one has to scroll back up to find.
    BarButton {
        id: back

        objectName: "series-back"
        x: Widths.shelfMargin
        y: 12
        visible: Navigation.canGoBack
        source: "assets/icons/arrow_back.svg"
        label: Navigation.backLabel
        onTriggered: Navigation.back()
    }

    Flickable {
        id: scroll

        anchors.fill: parent
        anchors.topMargin: back.visible ? back.y + back.height : 0
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
                height: 208
                // The one screen with nothing to keep — and only once the wait is long
                // enough to be one. On a server on this machine an answer lands in a few
                // milliseconds, and a shape that flashes for four of them says something
                // heavy happened where nothing did. Two hundred milliseconds is the floor
                // the shelf's own skeleton is held to.
                visible: !view.page.available && view.page.loading && waited.triggered
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
                editions: view.page.editions
                seriesId: view.page.identifier
                onUniverseAsked: {
                    Shelf.narrowTo({ "universe": [view.page.universe] })
                    Navigation.open(Navigation.Shelf, {})
                }
                onEditionChosen: seriesId => view.goToEdition(seriesId)
                onReimportAsked: view.reimportAsked("")
            }

            ResumeBand {
                width: parent.width
                // The shelf's band asked for this model at startup and it has been kept up to
                // date since. This one is rebuilt every time the page is opened, so asking
                // here is asking again for what is already held.
                reloadOnCompleted: false
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

                        required property int index
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
                                last: line.index === view.volumes.count - 1
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

                // The shelf's own columns, so a volume is drawn at the size a series is: the
                // grid exists to show covers, and a fixed width had them at a third of the
                // size of the ones on the wall this page was opened from.
                readonly property real side: (width + spacing) / Math.max(1, Widths.shelfColumns)
                                             - spacing

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
                facts: view.page.facts
                genres: view.page.genres
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
