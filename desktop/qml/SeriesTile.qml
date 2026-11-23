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
    required property int volumeCount
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

    /// Told by `Shelf.forgetting` just before this tile's row goes: the tile leaves with the
    /// choreography below. A tile whose row goes for any other reason — a filter that no longer
    /// holds it — is simply taken away, as it always was.
    property bool erasing: false
    /// The garnet veil and its ring, 0 to 1: what says « this one » for the first 160 ms.
    property real veil: 0
    property real ring: 0
    readonly property bool slides: Preferences.motion === Preferences.Slide

    readonly property bool hovered: pointer.hovered || commands.opened
    /// Picked with others, to be commanded together: see `Selection`.
    readonly property bool picked: Selection.ids.includes(tile.seriesId)

    /// What a click or Enter does: opens, unless something is already picked — then it picks
    /// one more, as it does in every file manager, so that a selection is built by clicking
    /// on and not by holding a key down for each cover.
    function activate() {
        if (Selection.active)
            Selection.toggle(tile.seriesId)
        else
            tile.opened()
    }

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
    // so the menu of the tile under it opens where a desktop reader expects it to — and so
    // does Enter, which went past the tile, then the grid, then the page, and opened nothing:
    // the keys could reach every cover and open none of them.
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                || event.key === Qt.Key_Space) {
            if (event.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))
                Selection.toggle(tile.seriesId)
            else
                tile.activate()
            event.accepted = true
        } else if (event.key === Qt.Key_Menu
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
    Accessible.selected: tile.picked

    Connections {
        target: Shelf

        function onForgetting(seriesId) {
            if (seriesId !== tile.seriesId || Preferences.motion === Preferences.Instant)
                return
            tile.erasing = true
            leaving.start()
        }
    }

    // 800 ms, in four beats: the veil and a small jump, the tilt, the fall — 570 ms, while the
    // row is still in the model, `Shelf` keeping it for as long — then the gap closing, which is
    // the grid's, while the tile finishes fading. The row goes at 570 ms and `delayRemove` keeps
    // the tile on screen until its last beat is over. « Fondu » keeps only the opacity: no
    // jump, no tilt, no fall.
    GridView.onRemove: if (tile.erasing) finishing.start()

    SequentialAnimation {
        id: finishing

        PropertyAction { target: tile; property: "GridView.delayRemove"; value: true }
        NumberAnimation { target: coverFrame; property: "opacity"; to: 0
                          duration: 230; easing.type: Easing.OutCubic }
        PropertyAction { target: tile; property: "GridView.delayRemove"; value: false }
    }

    SequentialAnimation {
        id: leaving

        ParallelAnimation {
            NumberAnimation { target: tile; property: "veil"; to: tile.slides ? 1 : 0
                              duration: 160; easing.type: Easing.OutCubic }
            NumberAnimation { target: tile; property: "ring"; to: tile.slides ? 1 : 0
                              duration: 160; easing.type: Easing.OutCubic }
            NumberAnimation { target: coverFrame; property: "scale"; to: tile.slides ? 1.08 : 1
                              duration: 160; easing.type: Easing.OutBack; easing.overshoot: 2 }
        }
        ParallelAnimation {
            NumberAnimation { target: tile; property: "ring"; to: 0
                              duration: 170; easing.type: Easing.InCubic }
            NumberAnimation { target: tile; property: "veil"; to: tile.slides ? 0.85 : 0
                              duration: 170; easing.type: Easing.InCubic }
            NumberAnimation { target: coverFrame; property: "rotation"; to: tile.slides ? -3 : 0
                              duration: 170; easing.type: Easing.InCubic }
            NumberAnimation { target: coverFrame; property: "y"
                              to: tile.viewportClearance + (tile.slides ? 10 : 0)
                              duration: 170; easing.type: Easing.InCubic }
            NumberAnimation { target: coverFrame; property: "opacity"; to: 0.75
                              duration: 170; easing.type: Easing.InCubic }
            NumberAnimation { target: tile; property: "lineFade"; to: 0
                              duration: 170; easing.type: Easing.InCubic }
        }
        ParallelAnimation {
            NumberAnimation { target: tile; property: "veil"; to: tile.slides ? 0.55 : 0
                              duration: 240; easing.type: Easing.OutQuad }
            NumberAnimation { target: coverFrame; property: "rotation"; to: tile.slides ? -6 : 0
                              duration: 240; easing.type: Easing.OutQuad }
            NumberAnimation { target: coverFrame; property: "y"
                              to: tile.viewportClearance + (tile.slides ? 30 : 0)
                              duration: 240; easing.type: Easing.OutQuad }
            NumberAnimation { target: coverFrame; property: "scale"; to: tile.slides ? 1.03 : 1
                              duration: 240; easing.type: Easing.OutQuad }
            NumberAnimation { target: coverFrame; property: "opacity"; to: 0.2
                              duration: 240; easing.type: Easing.OutQuad }
        }
    }

    /// How much of its two lines is left while it leaves: they go first.
    property real lineFade: 1

    /// How far below its place the cover stands while it arrives, in pixels. Its own property
    /// and not the cover's `y`: that one is bound to the clearance, and an animation of it would
    /// break the binding for a tile that, unlike an erased one, stays.
    property real arrivalDrop: 0

    // An erasure played backwards, 800 ms like it: the tile is first what the erasure left of
    // it (faint, fallen, tilted), rises through the second and third beats, and settles from
    // the first one's jump. The 230 ms of the first are the neighbours' glide, which is the
    // grid's (`ClosesTheGap`). Only for a row the shelf added because an import brought it, and
    // only where the grid grants it (`claimArrival`): « Fondu » keeps the opacity alone, and
    // `Instant` no beat at all.
    GridView.onAdd: {
        const shelf = GridView.view
        if (shelf && shelf.arriving && Preferences.motion !== Preferences.Instant
                && shelf.claimArrival(tile)) {
            arrival.start()
        }
    }

    SequentialAnimation {
        id: arrival

        PropertyAction { target: coverFrame; property: "opacity"; value: 0 }
        PropertyAction { target: coverFrame; property: "rotation"; value: tile.slides ? -6 : 0 }
        PropertyAction { target: coverFrame; property: "scale"; value: tile.slides ? 1.03 : 1 }
        PropertyAction { target: tile; property: "arrivalDrop"; value: tile.slides ? 30 : 0 }
        PropertyAction { target: tile; property: "lineFade"; value: 0 }
        NumberAnimation { target: coverFrame; property: "opacity"; to: 0.2
                          duration: 230; easing.type: Easing.OutCubic }
        ParallelAnimation {
            NumberAnimation { target: coverFrame; property: "opacity"; to: 0.75
                              duration: 240; easing.type: Easing.InQuad }
            NumberAnimation { target: coverFrame; property: "rotation"; to: tile.slides ? -3 : 0
                              duration: 240; easing.type: Easing.InQuad }
            NumberAnimation { target: tile; property: "arrivalDrop"; to: tile.slides ? 10 : 0
                              duration: 240; easing.type: Easing.InQuad }
            NumberAnimation { target: coverFrame; property: "scale"; to: tile.slides ? 1.08 : 1
                              duration: 240; easing.type: Easing.InQuad }
        }
        ParallelAnimation {
            NumberAnimation { target: coverFrame; property: "opacity"; to: 1
                              duration: 170; easing.type: Easing.OutCubic }
            NumberAnimation { target: coverFrame; property: "rotation"; to: 0
                              duration: 170; easing.type: Easing.OutCubic }
            NumberAnimation { target: tile; property: "arrivalDrop"; to: 0
                              duration: 170; easing.type: Easing.OutCubic }
            NumberAnimation { target: tile; property: "lineFade"; to: 1
                              duration: 170; easing.type: Easing.OutCubic }
        }
        NumberAnimation { target: coverFrame; property: "scale"; to: 1
                          duration: 160; easing.type: Easing.OutCubic }
    }

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
        acceptedModifiers: Qt.NoModifier
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: tile.activate()
    }

    PickHandler {
        onToggled: Selection.toggle(tile.seriesId)
    }

    // The secondary button opens the same menu as the three dots, under the pointer.
    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: point => commands.showAt(tile, point.position)
    }

    Item {
        id: coverFrame

        objectName: "cover-frame-" + tile.seriesId
        x: tile.viewportClearance
        y: tile.viewportClearance + tile.arrivalDrop
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

        // The garnet is the application's one word for « this will not come back »; here it is
        // on the cover for the length of a breath and nowhere else in the tile.
        Rectangle {
            objectName: "erasing-veil-" + tile.seriesId
            anchors.fill: parent
            radius: Theme.coverRadius
            color: Theme.alert
            opacity: 0.35 * tile.veil
            visible: opacity > 0
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: Theme.coverRadius + 2
            color: "transparent"
            border.color: Theme.alert
            border.width: 2
            opacity: tile.ring
            visible: opacity > 0
        }

        ReadCheck {
            key: tile.seriesId
            finished: tile.finished
            coverWidth: tile.coverWidth
        }

        PickMark {
            objectName: "picked-" + tile.seriesId
            picked: tile.picked
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
            // Faded in and out rather than switched: the dots appeared at the first pixel of
            // the hover, over the cover, as a flash. Hidden for good once the fade is over,
            // so that a button nobody sees is not one the pointer or the keys can reach.
            opacity: tile.hovered || tile.selected || opened ? 1 : 0
            visible: opacity > 0
            Behavior on opacity {
                NumberAnimation {
                    duration: Preferences.motion === Preferences.Instant ? 0 : 140
                    easing.type: Easing.OutCubic
                }
            }
            seriesId: tile.seriesId
            edition: tile.name !== tile.work
            label: tile.name
            onReimportAsked: ImportOffers.wantReimport("SERIES", tile.seriesId, tile.name, tile.volumeCount)
            canAddEditions: tile.oneShotEntry.length === 0
            onEditionsAddAsked: ImportOffers.wantReimport("SERIES_NEW_EDITIONS", tile.seriesId, tile.name, 0)
        }
    }

    // The grid measured its cell from these same lines; the tile draws with them, so the
    // cell and its words are one decision and not two copies of it.
    TileLines {
        id: lines
    }

    Text {
        id: title

        objectName: "title-" + tile.seriesId
        opacity: tile.lineFade
        anchors.left: coverFrame.left
        anchors.right: coverFrame.right
        anchors.top: coverFrame.bottom
        anchors.topMargin: lines.above
        // No height of its own: two lines at most, and as tall as they came out. Held to a
        // literal 40, a name at a larger text size lost its second line's descenders.
        textFormat: Text.PlainText
        text: tile.name
        color: Theme.ink
        font: lines.titleFont
        lineHeightMode: Text.FixedHeight
        lineHeight: lines.titleLine
        wrapMode: Text.Wrap
        maximumLineCount: lines.titleLines
        elide: Text.ElideRight
    }

    Text {
        objectName: "volumes-" + tile.seriesId
        opacity: tile.lineFade
        anchors.left: coverFrame.left
        anchors.right: coverFrame.right
        anchors.top: title.bottom
        anchors.topMargin: lines.between
        textFormat: Text.PlainText
        text: tile.volumes
        color: Theme.inkFaint
        font: lines.countFont
        elide: Text.ElideRight
    }
}
