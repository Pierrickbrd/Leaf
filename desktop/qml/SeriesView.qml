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
    signal reimportAsked(string kind, string id, string name, int volumes)
    signal chapterAsked(string entryId, string name)
    /// The keys walked past the last stop of this page, or before its first: the shell
    /// carries the focus round to the bar, as it does from the shelf.
    signal navigationBoundary(bool forward)

    /// Takes the focus by this page's first stop going forward, its last coming back. The
    /// shell calls it from the bar. This page had none, and the bar's last stop handed the
    /// focus straight back to its first: no key reached a volume, a tab or the way back.
    function takeFocus(forward) {
        return walk.take(forward)
    }

    /// The menu of the series, from anywhere on the page: the Menu key, or Shift+F10, said
    /// to the page rather than to a row of it.
    function showCommands() {
        view.reveal(header)
        header.showCommands()
    }

    /// The stop the keys reached, brought into the scroll. The page scrolls, not the rows,
    /// so a focus given to the twentieth volume was a ring drawn below the window.
    function reveal(stop) {
        const top = stop.mapToItem(scroll.contentItem, 0, 0).y
        const room = 12
        if (top - room < scroll.contentY)
            scroll.contentY = Math.max(0, top - room)
        else if (top + stop.height + room > scroll.contentY + scroll.height)
            scroll.contentY = Math.min(Math.max(0, scroll.contentHeight - scroll.height),
                                       top + stop.height + room - scroll.height)
    }

    KeyWalk {
        id: walk

        region: view
        onWentPast: forward => view.navigationBoundary(forward)
        onReached: stop => view.reveal(stop)
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Menu
                || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            view.showCommands()
            event.accepted = true
            return
        }
        event.accepted = walk.step(event)
    }

    /// Changing edition is not a navigation: the page and its list are re-pointed under the
    /// same header. Here and not in two places, because the switcher in the header and the
    /// last tab both do it and the day one of them learns something the other must too.
    function goToEdition(seriesId) {
        // The list follows when the page has answered, with the gaps and the arcs of this
        // edition: pointed now, it was woven with the last edition's and then with none.
        view.page.point(seriesId)
    }

    /// The shelf, narrowed to one genre: what pressing the universe does, for a genre. Here
    /// and not in the header, beside what the universe does.
    function showGenre(genre) {
        Shelf.narrowTo({ "genre": [genre] })
        Navigation.open(Navigation.Shelf, {})
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

    /// Which of the two drawings of the volumes the tab and the choice show. Their own
    /// condition, and not the `visible` of the item that holds them: that one is false for as
    /// long as the whole page is hidden under another screen, and the lists were emptied —
    /// every row destroyed, the page shrunk and its scroll cut — and built again on the way
    /// back.
    readonly property bool listShown: view.current === view.volumesTab
                                      && !Preferences.volumesAsGrid
    readonly property bool gridShown: view.current === view.volumesTab
                                      && Preferences.volumesAsGrid

    /// What each tab was scrolled to when another one was chosen: a short tab shrinks the
    /// page, the scroll is cut to fit, and the long one came back at its top.
    property var offsets: ({})

    function chooseTab(which) {
        if (which === view.current)
            return
        const kept = view.offsets
        kept[view.current] = scroll.contentY
        view.offsets = kept
        view.current = which
        Qt.callLater(view.scrollTo, kept[which] ?? 0)
    }

    function scrollTo(offset) {
        scroll.contentY = Math.max(0, Math.min(offset,
                                               scroll.contentHeight - scroll.height))
    }

    /// Another series, on a page that was kept: its first tab, from its top.
    function startOver() {
        view.offsets = ({})
        view.current = view.volumesTab
        scroll.contentY = 0
    }

    Component.onCompleted: {
        if (view.bare)
            waited.restart()
    }

    // The way out, the page's own: the first thing under the bar, at the gutter, where the
    // settings' strip begins with the same button. Outside the flick: a control that leaves
    // the page cannot be one a reader has to scroll back up to find.
    PageStrip {
        id: head

        objectName: "series-strip"
        anchors.left: parent.left
        anchors.right: parent.right
        // The button's own condition and not `back.visible`, which is false for as long as this
        // strip is hidden: a page built while it is out of sight, as one kept under another
        // screen is, hid its strip for good and its way back with it.
        visible: Navigation.canGoBack

        BackButton {
            id: back
        }
    }

    Flickable {
        id: scroll

        anchors.fill: parent
        anchors.topMargin: head.visible ? head.height : 0
        anchors.leftMargin: Widths.shelfMargin
        anchors.rightMargin: Widths.shelfMargin
        contentWidth: width
        contentHeight: stack.implicitHeight + 32
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: stack

            // Under the strip of the way back, which left its `block` already; at `top` under
            // the bar, where every page starts, when there is no way back to lead it.
            y: head.visible ? 0 : Widths.space.top
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
                id: header

                width: parent.width
                visible: view.page.available
                universe: view.page.universe
                universeId: view.page.universeId
                work: view.page.work
                edition: view.page.edition
                cover: view.page.cover
                makers: view.page.description.makersLinked
                weights: view.page.description.weights
                genres: view.page.description.genres
                editionsLabel: view.page.editionsLabel
                editions: view.page.editions
                seriesId: view.page.identifier
                onUniverseAsked: {
                    Shelf.narrowTo({ "universe": [view.page.universe] })
                    Navigation.open(Navigation.Shelf, {})
                }
                onGenreAsked: genre => view.showGenre(genre)
                onEditionChosen: seriesId => view.goToEdition(seriesId)
                onReimportAsked: view.reimportAsked("SERIES", view.page.identifier, view.page.work,
                                                    view.page.volumeCount)
                onEditionsAddAsked: view.reimportAsked("SERIES_NEW_EDITIONS", view.page.identifier,
                                                       view.page.work, 0)
                onUniverseReimportAsked: view.reimportAsked("UNIVERSE", view.page.universeId,
                                                            view.page.universe, 0)
                onPeopleAsked: people.open()
            }

            PeopleDialog {
                id: people

                people: view.page.description.people
                heading: view.page.description.peopleTitle
                counted: view.page.description.peopleCount
            }

            ResumeBand {
                width: parent.width
                // The shelf's band asked for this model at startup and it has been kept up to
                // date since. This one is rebuilt every time the page is opened, so asking
                // here is asking again for what is already held.
                reloadOnCompleted: false
                // In the page's column, which has its margin already.
                inset: 0
                above: 0
                below: 0
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
                onChosen: which => view.chooseTab(which)
                onViewToggled: Preferences.volumesAsGrid = !Preferences.volumesAsGrid
                onSearched: text => view.volumes.searchFor(text)
            }

            // The list. The page scrolls, not a box inside it — one scroll, like the shelf,
            // and the cut is the bottom of the screen rather than a box with white under it.
            Column {
                id: asList

                width: parent.width
                spacing: 0
                visible: view.listShown

                Repeater {
                    // Emptied rather than hidden: a repeater in an invisible column still
                    // builds every one of its delegates, and this page has two of them.
                    model: view.listShown ? view.volumes : null

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
                        required property int chapters
                        required property string credits

                        width: parent.width
                        // Above its neighbours while the keys are on it: a row hovered below
                        // is drawn after the one above and its tint covered that one's ring.
                        z: line.item && line.item.activeFocus ? 1 : 0
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
                                chapters: line.chapters
                                credits: line.credits
                                onReimportAsked: view.reimportAsked("ENTRY", line.entryId, line.number, 0)
                                onChapterAsked: view.chapterAsked(line.entryId, line.number)
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
                    textFormat: Text.PlainText
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
            // Every borrowed cover of the grid is this series' picture: measured once here, for
            // the colour of every number written on it (`CoverTone`).
            CoverTone {
                id: tone

                source: view.gridShown ? view.page.cover : ""
            }

            Flow {
                id: asGrid

                width: parent.width
                spacing: Widths.shelfGap
                visible: view.gridShown

                // The shelf's own columns, so a volume is drawn at the size a series is: the
                // grid exists to show covers, and a fixed width had them at a third of the
                // size of the ones on the wall this page was opened from.
                readonly property real side: (width + spacing) / Math.max(1, Widths.shelfColumns)
                                             - spacing

                Repeater {
                    model: view.gridShown ? view.volumes : null

                    Loader {
                        id: cell

                        required property string entryId
                        required property string number
                        required property string title
                        required property string caption
                        required property bool borrowed
                        required property string coverNumber
                        required property string kindWord
                        required property string readPercent
                        required property string pages
                        required property string cover
                        required property string fileName
                        required property int chapters
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
                                caption: cell.caption
                                borrowed: cell.borrowed
                                coverNumber: cell.coverNumber
                                kindWord: cell.kindWord
                                readPercent: cell.readPercent
                                webtoon: view.page.webtoon
                                numberLight: tone.light
                                pages: cell.pages
                                cover: cell.cover
                                fileName: cell.fileName
                                state: cell.state
                                howFarRead: cell.howFarRead
                                seriesId: view.page.identifier
                                coverWidth: asGrid.side
                                coverHeight: asGrid.side * 1.5
                                chapters: cell.chapters
                                onReimportAsked: view.reimportAsked("ENTRY", cell.entryId, cell.number, 0)
                                onChapterAsked: view.chapterAsked(cell.entryId, cell.number)
                            }
                        }
                    }
                }
            }

            SeriesDescription {
                width: parent.width
                onPeopleAsked: people.open()
                visible: view.current === view.descriptionTab
                summary: view.page.description.summary
                facts: view.page.description.facts
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
