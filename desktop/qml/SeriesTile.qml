// One series cover and its two lines, shared by the shelf and both search views.

import QtQuick
import Leaf

Item {
    id: tile

    required property int index
    required property string seriesId
    required property string name
    required property string work
    required property string cover
    required property string medium
    required property string volumes
    required property bool inProgress
    required property bool finished
    required property real howFarRead
    /// The file to read when the series is one book on its own, empty otherwise.
    required property string oneShotEntry
    required property real cellWidth
    required property real cellHeight
    required property real coverWidth
    required property real coverHeight
    required property real viewportClearance
    required property bool selected

    signal pointerEntered()
    signal pointerExited()
    signal opened()
    signal reimportAsked()

    readonly property bool hovered: pointer.hovered || commands.opened

    /// Goes where a click on this tile leads: the reader on a book that is a whole book on its
    /// own — the third way in spec §01 names — and the series page for anything else, which
    /// for one book was a page of one volume to click again. Here and not in each grid, so the
    /// shelf and the search results never disagree.
    function go() {
        if (tile.oneShotEntry.length > 0)
            Navigation.open(Navigation.Reader,
                            { "series": tile.seriesId, "entry": tile.oneShotEntry })
        else
            Navigation.open(Navigation.Series, { "series": tile.seriesId })
    }

    objectName: "tile-" + seriesId
    width: cellWidth
    height: cellHeight

    // The keyboard reaches what the pointer reaches. A tile takes the focus from the grid,
    // so the menu of the tile under it opens where a desktop reader expects it to.
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Menu
                || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            commands.show()
            event.accepted = true
        }
    }

    Accessible.role: Accessible.ListItem
    Accessible.name: name
    Accessible.description: volumes
    Accessible.focusable: true
    Accessible.focused: selected

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        onEntered: tile.pointerEntered()
        onExited: tile.pointerExited()
    }

    HoverHandler { id: pointer }

    // Beside the hover area rather than inside it: that one takes no button on purpose, so
    // that a stationary pointer never steals the emphasis a key just gave somewhere else.
    TapHandler {
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: tile.opened()
    }

    Item {
        id: coverFrame

        objectName: "cover-frame-" + tile.seriesId
        x: tile.viewportClearance
        y: tile.viewportClearance
        width: tile.coverWidth
        height: tile.coverHeight

        CoverShadow {
            under: tile.seriesId
        }

        RoundedCover {
            id: clippedCover

            objectName: "clipped-cover-" + tile.seriesId
            anchors.fill: parent
            source: tile.cover
            pictureName: "cover-" + tile.seriesId

            // How far through the series, drawn as the fraction it is — and full, with the
            // check below, once it is finished. It drew nothing then, which is what a series
            // never opened looks like too.
            ReadBar {
                key: tile.seriesId
                started: tile.inProgress
                finished: tile.finished
                howFarRead: tile.howFarRead
            }
        }

        CoverSkin { }

        ReadCheck {
            key: tile.seriesId
            finished: tile.finished
            coverWidth: tile.coverWidth
        }

        FocusRing {
            objectName: "focus-" + tile.seriesId
            visible: tile.selected
        }

        // At the hover and at the focus, and never permanent: fifty « … » over fifty
        // illustrations are fifty stains on the one wall this application exists to show.
        // Inside the ring rather than beside it — the ring goes round the cover alone.
        CommandMenu {
            id: commands

            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 4
            coverWidth: tile.coverWidth
            visible: tile.hovered || tile.selected || opened
            seriesId: tile.seriesId
            label: tile.name
            onReimportAsked: tile.reimportAsked()
        }
    }

    Text {
        id: title

        objectName: "title-" + tile.seriesId
        anchors.left: coverFrame.left
        anchors.right: coverFrame.right
        anchors.top: coverFrame.bottom
        anchors.topMargin: 6
        height: Math.min(implicitHeight, 40)
        text: tile.name
        color: Theme.ink
        font.family: Theme.displayFamily
        font.pixelSize: 16
        font.weight: Font.DemiBold
        lineHeightMode: Text.FixedHeight
        lineHeight: 20
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
        height: 18
        text: tile.volumes
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: 14
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
