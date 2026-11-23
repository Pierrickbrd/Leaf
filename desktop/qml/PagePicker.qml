// Every page of a volume as a thumbnail, and one is the cover. The same grid chooses among the
// volumes of a series when it is given `covers`: the choice of a cover is one gesture, and a
// second picker would have been a second set of keys to keep in step with the first.
//
// A number typed was a guess: the page that makes the cover is the one that is recognised, and
// counting up to it is what a reader does not do. The thumbnails are the pages' own route
// asked narrow (`/entries/{id}/pages/{n}?width=`), so nothing is prepared for this window.
// Arrows walk the grid, Return or Space choose, Escape leaves it as it was.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Leaf

LeafDialog {
    id: picker

    required property var words
    required property string pagesUrl
    required property int pageCount
    /// `[{ cover, label }]` when it is the volumes of a series that are laid out, `null` for
    /// the pages of one volume.
    property var covers: null
    property string heading: picker.words.choosePageTitle

    readonly property int count: picker.covers ? picker.covers.length : picker.pageCount

    /// The page chosen, counted from 0 like the contract — or the place of the volume in
    /// `covers`.
    signal chosen(int index)

    surfaceName: picker.objectName + "-surface"
    drawnWidth: 720
    initialFocus: grid

    Text {
        width: parent.width
        textFormat: Text.PlainText
        text: picker.heading
        color: Theme.ink
        font.family: Theme.displayFamily
        font.pixelSize: Widths.scale.title(20)
        font.weight: Font.Bold
        wrapMode: Text.WordWrap
    }

    GridView {
        id: grid

        objectName: picker.objectName + "-grid"
        width: parent.width
        height: Widths.scale.size(360)
        cellWidth: Math.floor(width / Math.max(1, Math.floor(width / Widths.scale.size(120))))
        cellHeight: Widths.scale.size(190)
        model: picker.opened ? picker.count : 0
        clip: true
        activeFocusOnTab: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollIndicator.vertical: ScrollIndicator { }

        function choose(index) {
            picker.chosen(index)
            picker.close()
        }

        Keys.onPressed: event => {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                    || event.key === Qt.Key_Space) {
                if (!event.isAutoRepeat && grid.currentIndex >= 0)
                    grid.choose(grid.currentIndex)
                event.accepted = true
            }
        }

        delegate: Item {
            id: cell

            required property int index

            width: grid.cellWidth
            height: grid.cellHeight

            Item {
                anchors.fill: parent
                anchors.margins: Widths.scale.size(6)

                RoundedCover {
                    objectName: picker.objectName + "-page-" + cell.index
                    anchors.fill: parent
                    anchors.bottomMargin: Widths.scale.size(20)
                    radius: Widths.scale.size(Theme.coverRadius)
                    source: picker.covers ? picker.covers[cell.index].cover + "?width=240"
                                          : picker.pagesUrl + cell.index + "?width=240"
                }

                Text {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    text: picker.covers ? picker.covers[cell.index].label
                                    : picker.words.pageNumber.replace("%1", cell.index + 1)
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -Widths.scale.size(3)
                    radius: Widths.scale.size(Theme.coverRadius)
                    color: "transparent"
                    border.color: Theme.emerald
                    border.width: 2
                    visible: grid.activeFocus && grid.currentIndex === cell.index
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: grid.choose(cell.index)
                }
            }
        }
    }

    commands: [
        LeafTextAction {
            objectName: picker.objectName + "-cancel"
            scaled: true
            label: picker.words.cancel
            activeFocusOnTab: true
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: picker.close()
        }
    ]
}
