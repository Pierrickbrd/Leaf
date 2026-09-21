// The non-series matches: compact rows above the ordinary shelf grid.

import QtQuick
import QtQuick.Controls
import Qt5Compat.GraphicalEffects
import Leaf

Item {
    id: results

    property var sourceModel: Search
    property var shelfModel: Shelf
    required property NavigationCursor navigationCursor

    signal wentPast(bool forward)

    objectName: "search-results"
    visible: sourceModel.active
    height: visible && content.visible ? content.implicitHeight + 2 * Widths.shelfGap : 0

    function stops() {
        const all = []
        for (let i = 0; i < content.children.length; ++i) {
            const one = content.children[i]
            if (one && one.visible && one.activeFocusOnTab)
                all.push(one)
        }
        return all
    }

    function at(all) {
        for (let i = 0; i < all.length; ++i) {
            if (all[i].activeFocus)
                return i
        }
        return -1
    }

    function takeFocus(forward) {
        const all = stops()
        if (all.length === 0)
            return false
        const position = forward ? 0 : all.length - 1
        navigationCursor.useKeyboard(results, position)
        all[position].forceActiveFocus(
            forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
        return true
    }

    function move(key, modifiers) {
        const all = stops()
        let here = navigationCursor.region === results ? navigationCursor.index : at(all)
        if (here < 0 || all.length === 0)
            return false

        const back = key === Qt.Key_Left || key === Qt.Key_Up
                || key === Qt.Key_Backtab
                || (key === Qt.Key_Tab && (modifiers & Qt.ShiftModifier))
        const on = key === Qt.Key_Right || key === Qt.Key_Down || key === Qt.Key_Tab
        if (!back && !on)
            return false

        const next = here + (on ? 1 : -1)
        if (next < 0 || next >= all.length) {
            navigationCursor.clear(results)
            wentPast(on)
            return true
        }

        navigationCursor.useKeyboard(results, next)
        all[next].forceActiveFocus(on ? Qt.TabFocusReason : Qt.BacktabFocusReason)
        return true
    }

    Keys.onPressed: event => {
        event.accepted = move(event.key, event.modifiers)
    }

    Connections {
        target: results.navigationCursor

        function onNavigationKeyRequested(owner, key, modifiers) {
            if (owner === results)
                results.move(key, modifiers)
        }

        function onResetRequested(owner) {
            if (owner === results)
                results.navigationCursor.clear(results)
        }
    }

    Connections {
        target: results.sourceModel

        function onChanged() {
            if (results.navigationCursor.region === results
                    && results.navigationCursor.index >= results.stops().length) {
                results.navigationCursor.clear(results)
            }
        }
    }

    Column {
        id: content

        y: Widths.shelfGap
        width: parent.width
        spacing: 2
        visible: sourceModel.total > 0 || sourceModel.remaining > 0
                 || sourceModel.outsideFilters.length > 0
                 || sourceModel.suggestion.length > 0
                 || sourceModel.trouble.length > 0 || sourceModel.loading

        Text {
            objectName: "search-files-heading"
            x: Widths.shelfMargin + 4
            width: parent.width - 2 * (Widths.shelfMargin + 4)
            height: visible ? 24 : 0
            visible: sourceModel.total > 0
            text: sourceModel.heading
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: 12
            font.weight: Font.Medium
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.8
            verticalAlignment: Text.AlignVCenter
        }

        Repeater {
            model: results.sourceModel

            delegate: Rectangle {
                id: resultRow

                required property int index
                required property string kind
                required property string resultId
                required property string label
                required property string seriesId
                required property string seriesName
                required property string entryId
                required property string cover

                readonly property bool hovered: resultPointer.hovered
                readonly property int navigationIndex: results.stops().indexOf(resultRow)
                readonly property bool selected:
                    results.navigationCursor.region === results
                    && results.navigationCursor.index === navigationIndex

                objectName: "search-result-" + resultId
                x: Widths.shelfMargin
                width: content.width - 2 * Widths.shelfMargin
                height: 56
                radius: Theme.buttonRadius
                color: selected || hovered ? Theme.onPaper : "transparent"
                activeFocusOnTab: true

                Accessible.role: Accessible.ListItem
                Accessible.name: label
                Accessible.description: seriesName
                Accessible.focusable: true
                Accessible.focused: selected

                onActiveFocusChanged: {
                    if (activeFocus)
                        results.navigationCursor.useKeyboard(results, navigationIndex)
                }

                HoverHandler {
                    id: resultPointer

                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: {
                        if (hovered) {
                            results.navigationCursor.usePointer(results,
                                                                resultRow.navigationIndex)
                        } else if (results.navigationCursor.mode
                                   === results.navigationCursor.pointerMode
                                   && results.navigationCursor.region === results
                                   && results.navigationCursor.index
                                      === resultRow.navigationIndex) {
                            results.navigationCursor.clear(results)
                        }
                    }
                }

                Item {
                    id: coverFrame

                    x: 7
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28
                    height: 42

                    Rectangle {
                        id: clippedCover

                        anchors.fill: parent
                        radius: 5
                        color: Theme.onPaper
                        clip: true
                        antialiasing: true

                        Image {
                            id: coverImage

                            anchors.fill: parent
                            source: resultRow.cover
                            asynchronous: true
                            cache: true
                            fillMode: Image.PreserveAspectCrop
                        }

                        ShaderEffectSource {
                            id: coverTexture
                            sourceItem: coverImage
                            hideSource: true
                            visible: false
                        }

                        OpacityMask {
                            anchors.fill: parent
                            source: coverTexture
                            maskSource: Rectangle {
                                width: clippedCover.width
                                height: clippedCover.height
                                radius: clippedCover.radius
                                antialiasing: true
                            }
                        }
                    }
                }

                Text {
                    id: resultName

                    anchors.left: coverFrame.right
                    anchors.leftMargin: 11
                    anchors.right: arrow.left
                    anchors.rightMargin: 10
                    anchors.top: parent.top
                    anchors.topMargin: 10
                    text: resultRow.label
                    color: Theme.ink
                    font.family: Theme.displayFamily
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    anchors.left: resultName.left
                    anchors.right: resultName.right
                    anchors.top: resultName.bottom
                    anchors.topMargin: 3
                    text: resultRow.seriesName
                    color: Theme.inkFaint
                    font.family: Theme.textFamily
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Text {
                    id: arrow

                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: "›"
                    color: Theme.inkFaint
                    font.family: Theme.textFamily
                    font.pixelSize: 23
                }

                FocusRing {
                    objectName: resultRow.objectName + "-focus"
                    cornerRadius: resultRow.radius
                    visible: selected
                }
            }
        }

        Rectangle {
            id: more

            objectName: "more-search-results"
            x: Widths.shelfMargin
            width: content.width - 2 * Widths.shelfMargin
            height: visible ? 34 : 0
            visible: sourceModel.remaining > 0
            radius: Theme.buttonRadius
            color: activeFocus || morePointer.hovered ? Theme.onPaper : "transparent"
            activeFocusOnTab: visible

            Accessible.role: Accessible.Button
            Accessible.name: sourceModel.moreLabel
            Accessible.focusable: true
            Accessible.focused: activeFocus
            Accessible.onPressAction: sourceModel.expand()

            Keys.onPressed: event => {
                if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                        || event.key === Qt.Key_Enter) {
                    sourceModel.expand()
                    event.accepted = true
                }
            }

            HoverHandler {
                id: morePointer
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: sourceModel.expand()
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 7
                anchors.verticalCenter: parent.verticalCenter
                text: sourceModel.moreLabel
                color: Theme.emerald
                font.family: Theme.textFamily
                font.pixelSize: 13
                font.weight: Font.Medium
            }

            FocusRing {
                cornerRadius: more.radius
            }
        }

        Text {
            objectName: "no-series-search-result"
            x: Widths.shelfMargin + 4
            width: content.width - 2 * (Widths.shelfMargin + 4)
            height: visible ? implicitHeight + 12 : 0
            visible: sourceModel.total > 0 && shelfModel.total === 0
                     && !shelfModel.loading
            text: sourceModel.noSeriesLabel
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }

        Rectangle {
            id: outside

            objectName: "outside-filter-results"
            x: Widths.shelfMargin
            width: content.width - 2 * Widths.shelfMargin
            height: visible ? outsideText.implicitHeight + 18 : 0
            visible: sourceModel.outsideFilters.length > 0
            radius: Theme.buttonRadius
            color: activeFocus || outsidePointer.hovered ? Theme.onPaper : "transparent"
            activeFocusOnTab: visible

            Accessible.role: Accessible.Button
            Accessible.name: sourceModel.outsideFilters
            Accessible.focusable: true
            Accessible.focused: activeFocus
            Accessible.onPressAction: sourceModel.clearFilters()

            Keys.onPressed: event => {
                if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                        || event.key === Qt.Key_Enter) {
                    sourceModel.clearFilters()
                    event.accepted = true
                }
            }

            HoverHandler {
                id: outsidePointer
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: sourceModel.clearFilters()
            }

            Text {
                id: outsideText

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 7
                anchors.verticalCenter: parent.verticalCenter
                text: sourceModel.outsideFilters
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: 13
                font.weight: Font.Medium
                wrapMode: Text.Wrap
            }

            FocusRing {
                cornerRadius: outside.radius
            }
        }

        Text {
            objectName: "approximate-search-result"
            x: Widths.shelfMargin + 7
            width: content.width - 2 * (Widths.shelfMargin + 7)
            height: visible ? implicitHeight + 8 : 0
            visible: sourceModel.suggestion.length > 0
            text: sourceModel.suggestion
            color: Theme.emerald
            font.family: Theme.textFamily
            font.pixelSize: 13
            font.weight: Font.Medium
            wrapMode: Text.Wrap
        }

        Text {
            objectName: "search-trouble"
            x: Widths.shelfMargin + 7
            width: content.width - 2 * (Widths.shelfMargin + 7)
            height: visible ? implicitHeight + 8 : 0
            visible: sourceModel.trouble.length > 0
            text: sourceModel.trouble
            color: Theme.alert
            font.family: Theme.textFamily
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }

        BusyIndicator {
            objectName: "search-loading"
            x: Math.round((content.width - width) / 2)
            width: 30
            height: visible ? 30 : 0
            running: sourceModel.loading
            visible: running
            palette.highlight: Theme.emerald
        }
    }
}
