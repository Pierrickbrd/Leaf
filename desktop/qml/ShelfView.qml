// The shelf's first pixels: a paged grid, and only the grid.
//
// The bar, filters, search and resume strip each have rules of their own and are deliberately
// absent. This screen draws exactly what Shelf already holds. GridView owns paging through
// QAbstractItemModel's fetchMore hooks, keyboard movement, and the lifetime of cover requests.
//
// A cover lives while its delegate is visible plus one cached row. Once it leaves that buffer,
// the delegate and its Image disappear together, which lets Qt cancel the request instead of
// leaving a hand-written queue to outlive the thing it was meant to draw.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Leaf

Item {
    id: root

    objectName: "shelf-view"

    // Replaceable only so the QML can be exercised with a small model in isolation. The
    // application never assigns it: it gets the one Shelf singleton for the whole run.
    property var sourceModel: Shelf
    readonly property int ringClearance: Theme.focusGap + 2
    // The shadow reaches 16 px left and right. Giving the viewport that full clearance keeps
    // the first and last elevations intact while the covers themselves stay on the 16 px edge.
    readonly property int viewportClearance: Widths.shelfMargin

    Component.onCompleted: {
        sourceModel.reload()
        grid.forceActiveFocus()
    }

    GridView {
        id: grid

        objectName: "shelf-grid"
        property int columns: Widths.shelfColumns
        readonly property real coverWidth: cellWidth - Widths.shelfGap
        readonly property real coverHeight: coverWidth * 1.5

        anchors.fill: parent

        model: root.sourceModel
        // Subtract both cover clearances from the viewport before sharing the useful width.
        // That keeps the first and last covers on the same 16 px edges in both palettes.
        cellWidth: (width - 2 * root.viewportClearance + Widths.shelfGap)
                   / Math.max(1, columns)
        // 34 px is the title's two 17 px lines; one-line names leave breathing room before
        // the next row rather than moving that row upward.
        cellHeight: Math.ceil(root.ringClearance + coverHeight + 6 + 34 + 2 + 16
                              + Widths.shelfGap)

        cacheBuffer: cellHeight
        keyNavigationEnabled: true
        keyNavigationWraps: false
        activeFocusOnTab: true
        focus: true
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        onCountChanged: {
            if (count > 0 && currentIndex < 0)
                currentIndex = 0
        }

        delegate: Item {
            id: tile

            required property int index
            required property string seriesId
            required property string name
            required property string work
            required property string cover
            required property string medium
            required property string volumes
            required property bool inProgress

            objectName: "tile-" + seriesId
            width: grid.cellWidth
            height: grid.cellHeight

            Accessible.role: Accessible.ListItem
            Accessible.name: name
            Accessible.description: volumes

            TapHandler {
                onTapped: {
                    grid.currentIndex = tile.index
                    grid.forceActiveFocus()
                }
            }

            Item {
                id: coverFrame

                objectName: "cover-frame-" + tile.seriesId
                x: root.viewportClearance
                y: root.viewportClearance
                width: grid.coverWidth
                height: grid.coverHeight

                CoverShadow {
                    seriesId: tile.seriesId
                }

                Rectangle {
                    id: clippedCover

                    objectName: "clipped-cover-" + tile.seriesId
                    anchors.fill: parent
                    radius: Theme.coverRadius
                    color: Theme.onPaper
                    clip: true
                    antialiasing: true

                    Image {
                        id: coverImage

                        objectName: "cover-" + tile.seriesId
                        anchors.fill: parent
                        source: tile.cover
                        asynchronous: true
                        cache: true
                        fillMode: Image.PreserveAspectCrop
                    }

                    Rectangle {
                        objectName: "in-progress-" + tile.seriesId
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 4
                        color: Theme.emerald
                        visible: tile.inProgress
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.coverRadius
                    color: "transparent"
                    border.color: Theme.rule
                    border.width: 1
                    antialiasing: true
                }

                FocusRing {
                    objectName: "focus-" + tile.seriesId
                    visible: tile.GridView.isCurrentItem && grid.activeFocus
                }
            }

            Text {
                id: title

                objectName: "title-" + tile.seriesId
                anchors.left: coverFrame.left
                anchors.right: coverFrame.right
                anchors.top: coverFrame.bottom
                anchors.topMargin: 6
                height: Math.min(implicitHeight, 34)
                text: tile.name
                color: Theme.ink
                font.family: Theme.displayFamily
                font.pixelSize: 14
                font.weight: Font.DemiBold
                lineHeightMode: Text.FixedHeight
                lineHeight: 17
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Text {
                objectName: "volumes-" + tile.seriesId
                anchors.left: coverFrame.left
                anchors.right: coverFrame.right
                anchors.top: title.bottom
                anchors.topMargin: 2
                height: 16
                text: tile.volumes
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }

    BusyIndicator {
        objectName: "shelf-first-load"
        anchors.centerIn: parent
        running: root.sourceModel.loading && grid.count === 0
        visible: running
        palette.highlight: Theme.emerald
    }

    BusyIndicator {
        objectName: "shelf-next-page"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Widths.shelfMargin
        running: root.sourceModel.loading && grid.count > 0
        visible: running
        palette.highlight: Theme.emerald
    }

    Rectangle {
        id: trouble

        objectName: "shelf-trouble"
        z: 2
        width: Math.max(0, Math.min(560, root.width - 2 * Widths.shelfMargin))
        height: troubleText.implicitHeight + 32
        x: Math.round((root.width - width) / 2)
        y: grid.count === 0
           ? Math.round((root.height - height) / 2)
           : root.height - height - Widths.shelfMargin
        radius: Theme.cardRadius
        color: Theme.surface
        visible: troubleText.text.length > 0

        Text {
            id: troubleText

            objectName: "shelf-trouble-text"
            anchors.fill: parent
            anchors.margins: 16
            text: root.sourceModel.trouble
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 15
            lineHeightMode: Text.FixedHeight
            lineHeight: 22
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
