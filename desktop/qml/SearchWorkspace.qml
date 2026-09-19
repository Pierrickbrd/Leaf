// Proposal one: a compact overview, then series and files in dedicated persistent scopes.

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

    Text {
        id: overviewSeriesHeading

        objectName: "overview-series-heading"
        x: Widths.shelfMargin + 4
        y: scopeTabs.height
        width: parent.width - 2 * (Widths.shelfMargin + 4)
        height: visible ? 28 : 0
        visible: workspace.mode === workspace.overviewMode
        text: Captions.seriesHeading
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: 12
        font.weight: Font.Medium
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
        verticalAlignment: Text.AlignVCenter
    }

    FilterPills {
        id: seriesFilters

        objectName: "search-series-filters"
        width: parent.width
        y: scopeTabs.height + overviewSeriesHeading.height
        // The overview is a summary, not a second control panel. Filters belong to both
        // complete result scopes because the same criteria narrow series and files alike.
        visible: workspace.mode !== workspace.overviewMode && showing
        reloadOnCompleted: false
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
        contentHeight: overviewContent.height + Widths.shelfGap
        clip: true
        boundsBehavior: Flickable.StopAtBounds

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
            spacing: 8

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

            SearchLink {
                id: allSeries

                objectName: "see-all-series"
                x: Widths.shelfMargin
                width: parent.width - 2 * Widths.shelfMargin
                visible: Shelf.total > overviewSeries.previewCount
                height: visible ? 38 : 0
                label: Captions.allSeriesLabel
                navigationCursor: workspace.navigationCursor
                onTriggered: workspace.selectMode(workspace.seriesMode)
                onWentPast: forward => workspace.stepFrom(allSeries, forward)
            }

            Rectangle {
                x: Widths.shelfMargin
                width: parent.width - 2 * Widths.shelfMargin
                height: 1
                color: Theme.rule
                visible: Shelf.total > 0 && Search.fileTotal > 0
            }

            Text {
                objectName: "search-files-heading"
                x: Widths.shelfMargin + 4
                width: parent.width - 2 * (Widths.shelfMargin + 4)
                height: 28
                text: Captions.filesHeading
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: 12
                font.weight: Font.Medium
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.8
                verticalAlignment: Text.AlignVCenter
            }

            FileResultsView {
                id: overviewFiles

                viewName: "search-overview-files"
                x: Widths.shelfMargin
                width: parent.width - 2 * Widths.shelfMargin
                height: visible && navigableCount > 0
                        ? navigableCount * rowHeight + (navigableCount - 1) * spacing : 0
                visible: Search.count > 0
                preview: true
                navigationCursor: workspace.navigationCursor
                onWentPast: forward => workspace.stepFrom(overviewFiles, forward)
                onBlankPressed: workspace.blankPressed()
            }

            SearchLink {
                id: allFiles

                objectName: "see-all-files"
                x: Widths.shelfMargin
                width: parent.width - 2 * Widths.shelfMargin
                visible: Search.fileTotal > overviewFiles.previewMaximum
                height: visible ? 38 : 0
                label: Captions.allFilesLabel
                navigationCursor: workspace.navigationCursor
                onTriggered: workspace.selectMode(workspace.filesMode)
                onWentPast: forward => workspace.stepFrom(allFiles, forward)
            }

            Text {
                objectName: "no-series-search-result"
                x: Widths.shelfMargin + 4
                width: parent.width - 2 * (Widths.shelfMargin + 4)
                visible: Search.fileTotal > 0 && Shelf.total === 0 && !Shelf.loading
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
                x: Widths.shelfMargin
                width: parent.width - 2 * Widths.shelfMargin
                visible: Captions.outsideFilters.length > 0
                height: visible ? 42 : 0
                label: Captions.outsideFilters
                navigationCursor: workspace.navigationCursor
                onTriggered: Search.clearFilters()
                onWentPast: forward => workspace.stepFrom(outsideFilters, forward)
            }

            Text {
                objectName: "approximate-search-result"
                x: Widths.shelfMargin + 4
                width: parent.width - 2 * (Widths.shelfMargin + 4)
                visible: Captions.suggestion.length > 0
                text: Captions.suggestion
                color: Theme.emerald
                font.family: Theme.textFamily
                font.pixelSize: 13
                font.weight: Font.Medium
                bottomPadding: 8
                wrapMode: Text.Wrap
            }

            Text {
                objectName: "search-trouble"
                x: Widths.shelfMargin + 4
                width: parent.width - 2 * (Widths.shelfMargin + 4)
                visible: Search.trouble.length > 0
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

    FileResultsView {
        id: filesGrid

        viewName: "search-all-files"
        x: Widths.shelfMargin
        y: seriesFilters.y + (seriesFilters.visible ? seriesFilters.height : 0)
        width: parent.width - 2 * Widths.shelfMargin
        height: Math.max(0, parent.height - y)
        visible: workspace.mode === workspace.filesMode
        navigationCursor: workspace.navigationCursor
        onWentPast: forward => workspace.stepFrom(filesGrid, forward)
        onBlankPressed: workspace.blankPressed()
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
        x: filesGrid.x
        y: filesGrid.y
        width: filesGrid.width
        leading: true
        shown: filesGrid.visible && !filesGrid.atYBeginning
    }

    ScrollVeil {
        objectName: "files-scroll-veil-bottom"
        x: filesGrid.x
        y: filesGrid.y + filesGrid.height - height
        width: filesGrid.width
        leading: false
        shown: filesGrid.visible && !filesGrid.atYEnd
    }
}
