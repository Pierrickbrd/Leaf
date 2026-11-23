// Proposal one: a compact overview, then series and files in dedicated persistent scopes.
//
// Laid out as every page is: the tabs on a `PageStrip` at the page's top, the headings at the
// column's edge, the files in a column held to `Widths.space.column`. On 23/10/2026 this one
// screen drew four left edges — 16 for the pills and covers, 20 for its headings, 24 for its
// links, 28 for a glyph — and put 8 pixels between the series and the files, the gap between
// two lines, where two sections stand `section` apart.

import QtQuick
import QtQuick.Controls
import Leaf

Item {
    id: workspace

    required property NavigationCursor navigationCursor

    signal navigationBoundary(bool forward)
    signal blankPressed()

    readonly property int overviewMode: 0
    readonly property int seriesMode: 1
    readonly property int filesMode: 2
    property int mode: overviewMode
    property string watchedQuery: Shelf.query
    /// Whether the reader has named a scope for the query in hand. Once they have, the answers
    /// still coming back leave them where they are.
    property bool scopeChosen: false

    /// The files scope is showing, so a row or a panel drawn above it counts files.
    readonly property bool showingFiles: mode === filesMode

    readonly property int seriesFound: Shelf.total
    readonly property int filesFound: Search.fileTotal
    readonly property bool stillAsking: Shelf.loading || Search.loading
    /// A question with no answer at all: not a series, not a file, not even one hidden behind
    /// a lit filter — that one says so itself, with the way back. Two headings reading « 0 »
    /// over nothing was all this screen said before (spacing audit, 23/10/2026).
    readonly property bool nothingFound: Shelf.query.length > 0 && !stillAsking
                                         && seriesFound === 0 && filesFound === 0
                                         && Search.trouble.length === 0
                                         && Captions.outsideFilters.length === 0

    objectName: "search-workspace"
    clip: true

    function regions() {
        const all = [scopeTabs]
        if (seriesFilters.visible && seriesFilters.stops().length > 0)
            all.push(seriesFilters)

        if (mode === overviewMode) {
            if (overviewSeries.navigableCount > 0)
                all.push(overviewSeries)
            if (allSeries.visible)
                all.push(allSeries)
            if (overviewFiles.navigableCount > 0)
                all.push(overviewFiles)
            if (allFiles.visible)
                all.push(allFiles)
            if (outsideFilters.visible)
                all.push(outsideFilters)
        } else if (mode === seriesMode) {
            if (seriesGrid.navigableCount > 0)
                all.push(seriesGrid)
        } else if (filesGrid.navigableCount > 0) {
            all.push(filesGrid)
        }
        return all
    }

    function takeFocus(forward) {
        const all = regions()
        if (all.length === 0)
            return false
        for (let step = 0; step < all.length; ++step) {
            const at = forward ? step : all.length - 1 - step
            if (all[at].takeFocus(forward))
                return true
        }
        return false
    }

    function stepFrom(origin, forward) {
        navigationCursor.beginKeyboard(forward)
        const all = regions()
        const here = all.indexOf(origin)
        if (here >= 0) {
            for (let at = here + (forward ? 1 : -1);
                 at >= 0 && at < all.length; at += forward ? 1 : -1) {
                if (all[at].takeFocus(forward))
                    return
            }
        }
        navigationBoundary(forward)
    }

    function selectMode(selected) {
        if (selected < overviewMode || selected > filesMode)
            return
        // Named by the reader, so the counts still arriving must not move them afterwards.
        scopeChosen = true
        mode = selected
    }

    /// An overview of one section is that section with a heading above it and one more step
    /// to reach it. When only series or only files matched, the scope that holds them opens
    /// straight away — until the reader names one themselves, which settles it for the query.
    function settleScope() {
        if (scopeChosen || Shelf.loading || Search.loading || Shelf.query.length === 0)
            return
        const series = Shelf.total
        const files = Search.fileTotal
        if (series > 0 && files === 0)
            mode = seriesMode
        else if (files > 0 && series === 0)
            mode = filesMode
        else
            mode = overviewMode
    }

    onWatchedQueryChanged: {
        scopeChosen = false
        mode = overviewMode
        overview.contentY = 0
        seriesGrid.positionViewAtBeginning()
        filesGrid.positionViewAtBeginning()
    }

    onSeriesFoundChanged: settleScope()
    onFilesFoundChanged: settleScope()
    onStillAskingChanged: settleScope()

    SearchScopeTabs {
        id: scopeTabs

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        navigationCursor: workspace.navigationCursor
        currentIndex: workspace.mode
        onSelected: index => workspace.selectMode(index)
        onWentPast: forward => workspace.stepFrom(scopeTabs, forward)
    }

    // At the page's edge, where the covers under it start, and not four pixels in from it.
    SectionHeading {
        id: overviewSeriesHeading

        objectName: "overview-series-heading"
        x: Widths.space.gutter
        y: scopeTabs.height
        width: Math.max(0, parent.width - 2 * Widths.space.gutter)
        visible: workspace.mode === workspace.overviewMode && !workspace.nothingFound
        text: Captions.seriesHeading
    }

    FilterPills {
        id: seriesFilters

        objectName: "search-series-filters"
        width: parent.width
        y: scopeTabs.height + (overviewSeriesHeading.visible ? overviewSeriesHeading.height : 0)
        // The overview is a summary, not a second control panel. Filters belong to both
        // complete result scopes because the same criteria narrow series and files alike.
        visible: workspace.mode !== workspace.overviewMode && showing
        reloadOnCompleted: false
        // Under the tabs' strip, which already left its block below it.
        topPadding: 0
        // In the files scope the row sits above files, so it counts files.
        overFiles: workspace.mode === workspace.filesMode
        onWentPast: forward => workspace.stepFrom(seriesFilters, forward)
    }

    Flickable {
        id: overview

        objectName: "search-overview"
        x: 0
        y: seriesFilters.y + (seriesFilters.visible ? seriesFilters.height : 0)
        width: parent.width
        height: Math.max(0, parent.height - y)
        visible: workspace.mode === workspace.overviewMode
        contentWidth: width
        contentHeight: overviewContent.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        FollowsFocus { flick: overview }

        PointHandler {
            target: null
            onActiveChanged: {
                if (active)
                    workspace.blankPressed()
            }
        }

        Column {
            id: overviewContent

            width: overview.width

            // Full width: its cells keep the gutter, as the shelf's do.
            SearchSeriesGrid {
                id: overviewSeries

                viewName: "search-overview-series"
                width: parent.width
                height: visible ? cellHeight : 0
                visible: Shelf.count > 0
                preview: true
                navigationCursor: workspace.navigationCursor
                onWentPast: forward => workspace.stepFrom(overviewSeries, forward)
                onBlankPressed: workspace.blankPressed()
            }

            // The way to the rest of the series, as wide as the covers it continues.
            PageColumn {
                width: parent.width
                topPadding: 0
                bottomPadding: 0

                SearchLink {
                    id: allSeries

                    objectName: "see-all-series"
                    visible: Shelf.total > overviewSeries.previewCount
                    height: visible ? 38 : 0
                    label: Captions.allSeriesLabel
                    navigationCursor: workspace.navigationCursor
                    onTriggered: workspace.selectMode(workspace.seriesMode)
                    onWentPast: forward => workspace.stepFrom(allSeries, forward)
                }
            }

            // Everything else is read, so it stands in the page's column.
            PageColumn {
                id: overviewColumn

                objectName: "search-overview-column"
                width: parent.width
                bounded: true
                topPadding: 0

                Column {
                    width: overviewColumn.measure

                    // Two sections, so a section apart — with the rule in the middle of it.
                    Item {
                        objectName: "search-sections-gap"
                        width: parent.width
                        height: visible ? Widths.space.section : 0
                        visible: Shelf.total > 0 && Search.fileTotal > 0

                        Rectangle {
                            y: Math.round((parent.height - height) / 2)
                            width: parent.width
                            height: 1
                            color: Theme.rule
                        }
                    }

                    SectionHeading {
                        objectName: "search-files-heading"
                        width: parent.width
                        visible: !workspace.nothingFound
                        text: Captions.filesHeading
                    }

                    FileResultsView {
                        id: overviewFiles

                        viewName: "search-overview-files"
                        width: parent.width
                        height: visible && navigableCount > 0
                                ? navigableCount * rowHeight + (navigableCount - 1) * spacing : 0
                        visible: Search.count > 0
                        preview: true
                        navigationCursor: workspace.navigationCursor
                        onWentPast: forward => workspace.stepFrom(overviewFiles, forward)
                        onBlankPressed: workspace.blankPressed()
                    }

                    // Room for the link's ring: right under the last row it touched its
                    // thumbnail.
                    Item {
                        width: 1
                        height: allFiles.visible ? Widths.space.compact : 0
                    }

                    SearchLink {
                        id: allFiles

                        objectName: "see-all-files"
                        visible: Search.fileTotal > overviewFiles.previewMaximum
                        height: visible ? 38 : 0
                        label: Captions.allFilesLabel
                        navigationCursor: workspace.navigationCursor
                        onTriggered: workspace.selectMode(workspace.filesMode)
                        onWentPast: forward => workspace.stepFrom(allFiles, forward)
                    }

                    Text {
                        objectName: "no-series-search-result"
                        width: parent.width
                        visible: Search.fileTotal > 0 && Shelf.total === 0 && !Shelf.loading
                        textFormat: Text.PlainText
                        text: Captions.noSeriesLabel
                        color: Theme.inkSoft
                        font.family: Theme.textFamily
                        font.pixelSize: 13
                        bottomPadding: 8
                        wrapMode: Text.Wrap
                    }

                    SearchLink {
                        id: outsideFilters

                        objectName: "outside-filter-results"
                        visible: Captions.outsideFilters.length > 0
                        height: visible ? 42 : 0
                        label: Captions.outsideFilters
                        navigationCursor: workspace.navigationCursor
                        onTriggered: Search.clearFilters()
                        onWentPast: forward => workspace.stepFrom(outsideFilters, forward)
                    }

                    Text {
                        objectName: "approximate-search-result"
                        width: parent.width
                        visible: Captions.suggestion.length > 0
                        text: Captions.suggestion
                        // It carries a series' name, which is the files' words.
                        textFormat: Text.PlainText
                        color: Theme.emerald
                        font.family: Theme.textFamily
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        bottomPadding: 8
                        wrapMode: Text.Wrap
                    }

                    Text {
                        objectName: "search-trouble"
                        width: parent.width
                        visible: Search.trouble.length > 0
                        textFormat: Text.PlainText
                        text: Search.trouble
                        color: Theme.alert
                        font.family: Theme.textFamily
                        font.pixelSize: 13
                        bottomPadding: 8
                        wrapMode: Text.Wrap
                    }

                    BusyIndicator {
                        objectName: "search-loading"
                        x: Math.round((parent.width - width) / 2)
                        width: 30
                        height: visible ? 30 : 0
                        running: Search.loading && Search.count === 0
                        visible: running
                        palette.highlight: Theme.emerald
                    }
                }
            }
        }
    }

    // Nothing found, said as the empty shelf says it, in the middle of the paper under the
    // tabs: one sentence, and not a headline — the shelf's headline is one of the three words
    // allowed to grow, and a search that found nothing is not the subject of a screen.
    PageColumn {
        id: unanswered

        objectName: "search-unanswered"
        y: scopeTabs.height
        width: parent.width
        height: Math.max(0, parent.height - y)
        topPadding: 0
        visible: workspace.mode === workspace.overviewMode && workspace.nothingFound

        ShelfNotice {
            objectName: "search-nothing"
            width: Math.max(0, Math.min(implicitWidth, unanswered.measure))
            height: implicitHeight
            x: Math.round((unanswered.measure - width) / 2)
            y: Math.round((unanswered.frame.height - height) / 2)
            glyph: "search"
            said: Captions.nothingFoundLabel
        }
    }

    SearchSeriesGrid {
        id: seriesGrid

        viewName: "search-all-series"
        x: 0
        y: seriesFilters.y + (seriesFilters.visible ? seriesFilters.height : 0)
        width: parent.width
        height: Math.max(0, parent.height - y)
        visible: workspace.mode === workspace.seriesMode
        navigationCursor: workspace.navigationCursor
        onWentPast: forward => workspace.stepFrom(seriesGrid, forward)
        onBlankPressed: workspace.blankPressed()
    }

    // Rows of words, so in the page's column and held to it: at 1920 a row ran 1888 pixels
    // with its words on the left and its chevron at the far end (audit, 23/10/2026).
    PageColumn {
        id: filesPage

        objectName: "search-files-column"
        y: seriesFilters.y + (seriesFilters.visible ? seriesFilters.height : 0)
        width: parent.width
        height: Math.max(0, parent.height - y)
        bounded: true
        topPadding: 0
        bottomPadding: 0

        FileResultsView {
            id: filesGrid

            viewName: "search-all-files"
            width: filesPage.measure
            height: filesPage.frame.height
            visible: workspace.mode === workspace.filesMode
            navigationCursor: workspace.navigationCursor
            onWentPast: forward => workspace.stepFrom(filesGrid, forward)
            onBlankPressed: workspace.blankPressed()
        }
    }

    ScrollVeil {
        objectName: "overview-scroll-veil-top"
        x: overview.x
        y: overview.y
        width: overview.width
        leading: true
        shown: overview.visible && !overview.atYBeginning
    }

    ScrollVeil {
        objectName: "overview-scroll-veil-bottom"
        x: overview.x
        y: overview.y + overview.height - height
        width: overview.width
        leading: false
        shown: overview.visible && !overview.atYEnd
    }

    ScrollVeil {
        objectName: "series-scroll-veil-top"
        x: seriesGrid.x
        y: seriesGrid.y
        width: seriesGrid.width
        leading: true
        shown: seriesGrid.visible && !seriesGrid.atYBeginning
    }

    ScrollVeil {
        objectName: "series-scroll-veil-bottom"
        x: seriesGrid.x
        y: seriesGrid.y + seriesGrid.height - height
        width: seriesGrid.width
        leading: false
        shown: seriesGrid.visible && !seriesGrid.atYEnd
    }

    ScrollVeil {
        objectName: "files-scroll-veil-top"
        x: filesPage.x + filesPage.frame.x
        y: filesPage.y + filesPage.frame.y
        width: filesGrid.width
        leading: true
        shown: filesGrid.visible && !filesGrid.atYBeginning
    }

    ScrollVeil {
        objectName: "files-scroll-veil-bottom"
        x: filesPage.x + filesPage.frame.x
        y: filesPage.y + filesPage.frame.y + filesGrid.height - height
        width: filesGrid.width
        leading: false
        shown: filesGrid.visible && !filesGrid.atYEnd
    }
}
