// One file of an edition, on a cover.
//
// Exactly the shelf's tile applied to a volume: the cover, the name under it, what it weighs
// under that. A series is drawn the same way everywhere, and a volume had no reason to escape
// it — which is why the marks are the shelf's marks too.
//
// **A bar and not a ring.** A cover has a bottom edge to lay a bar along; a line of a list has
// none, and that is the whole of the difference between this and `VolumeRow`. Finished, the
// bar is full *and* a check sits beside the words: the bar alone said two opposite things,
// never opened and done, by being absent in one case and complete in the other.
//
// **A borrowed cover carries its number.** A volume with no cover of its own shows its
// series' (`borrowed`), and on that picture, shared by every such volume, the number is what
// tells one from the next: see `CoverNumber`.
//
// Never opened carries neither, and no word either: an empty track drawn on every cover would
// be fifty grey lines on a wall of illustrations.

import QtQuick
import Leaf

Item {
    id: tile

    required property string entryId
    required property string number
    required property string title
    /// « 12 · Le Titre », « Tome 12 », « Chapitre 12 »: see `Words::tileCaption`.
    required property string caption
    required property string pages
    required property string cover
    required property string fileName
    required property int state
    required property real howFarRead
    /// No cover of its own: `cover` serves the series', and the number is written over it.
    required property bool borrowed
    /// Its number as written on that cover: a chapter's too, which has no `number`.
    required property string coverNumber
    /// « TOME » or « CHAPITRE » over that number, empty in an edition of one kind.
    required property string kindWord
    /// « 42 % », what a webtoon says in place of its pages.
    required property string readPercent
    /// A webtoon's chapter is one strip, and its page count says nothing a reader goes by.
    property bool webtoon: false
    /// The series' picture is pale at its foot even veiled: the number is written dark.
    property bool numberLight: false
    /// The edition this belongs to, which its menu commands it through.
    required property string seriesId
    required property real coverWidth
    required property real coverHeight

    readonly property bool missing: state === 3
    readonly property bool finished: state === 2
    readonly property bool started: state === 1
    readonly property bool hovered: pointer.hovered

    signal opened()
    signal reimportAsked()
    signal chapterAsked()
    property int chapters: 0

    // Opening a file is reading it: the line and the cover both lead to the reader, at the
    // page where reading stopped. Here rather than where the tile is placed, so the list and
    // the grid cannot open one book two ways.
    onOpened: Navigation.open(Navigation.Reader, { "series": tile.seriesId, "entry": tile.entryId })

    /// Picked with others, to be commanded together: see `Selection`.
    readonly property bool picked: Selection.ids.includes(tile.entryId)

    /// Opens, unless something is already picked: then it picks one more. See `SeriesTile`.
    function activate() {
        if (Selection.active)
            Selection.toggle(tile.entryId, tile.seriesId)
        else
            tile.opened()
    }

    objectName: "tile-volume-" + (entryId.length > 0 ? entryId : "missing-" + number)
    width: coverWidth
    height: coverHeight + 6 + 34 + 2 + 16

    Accessible.role: Accessible.ListItem
    Accessible.name: tile.caption
    Accessible.description: tile.missing ? tile.title : tile.pages
    Accessible.selected: tile.picked

    /// A stop of the page's walk — see `KeyWalk` — unless it is a hole, which opens nothing.
    readonly property bool keyStop: !tile.missing

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            if (event.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))
                Selection.toggle(tile.entryId, tile.seriesId)
            else
                tile.activate()
            event.accepted = true
        } else if (event.key === Qt.Key_Menu
                   || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            commands.show()
            event.accepted = true
        }
    }

    HoverHandler {
        id: pointer

        enabled: !tile.missing
        cursorShape: Qt.PointingHandCursor
    }

    // The cover answers the pointer. Without it the grid was a wall that did not react at
    // all: nothing said a tile could be opened, and the menu appearing in a corner was the
    // only sign anything had been noticed.
    scale: tile.hovered ? 1.03 : 1.0
    Behavior on scale {
        NumberAnimation {
            duration: 90
            easing.type: Easing.OutQuad
        }
    }

    TapHandler {
        gesturePolicy: TapHandler.ReleaseWithinBounds
        acceptedModifiers: Qt.NoModifier
        enabled: !tile.missing
        onTapped: tile.activate()
    }

    PickHandler {
        enabled: !tile.missing
        onToggled: Selection.toggle(tile.entryId, tile.seriesId)
    }

    // The secondary button opens the same menu as the three dots, under the pointer.
    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        enabled: !tile.missing
        onTapped: point => commands.showAt(tile, point.position)
    }

    Item {
        id: frame

        width: tile.coverWidth
        height: tile.coverHeight

        CoverShadow {
            under: tile.entryId
            visible: !tile.missing
        }

        RoundedCover {
            id: clipped

            anchors.fill: parent
            source: tile.cover
            visible: !tile.missing

            CoverVeil {
                full: tile.borrowed
            }

            CoverNumber {
                visible: tile.borrowed && tile.coverNumber.length > 0
                number: tile.coverNumber
                word: tile.kindWord
                light: tile.numberLight
                coverWidth: tile.coverWidth
            }

            ReadBar {
                key: tile.entryId
                started: tile.started
                finished: tile.finished
                howFarRead: tile.howFarRead
            }
        }

        // Nothing to show, so nothing is shown: a dashed frame in place of a cover, and the
        // word in the alert colour. A hole in a collection is drawn and left alone.
        Canvas {
            anchors.fill: parent
            visible: tile.missing
            antialiasing: true

            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.strokeStyle = Theme.rule
                context.lineWidth = 1
                context.setLineDash([4, 4])
                context.strokeRect(0.5, 0.5, width - 1, height - 1)
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 16
                textFormat: Text.PlainText
                text: tile.title
                color: Theme.alert
                font.family: Theme.textFamily
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }

        CoverSkin { visible: !tile.missing }

        // Around the cover, which is what the eye goes to, and lit by the tile's focus.
        FocusRing {
            objectName: tile.objectName + "-focus"
            focused: tile.activeFocus
        }

        PickMark {
            objectName: "picked-" + tile.entryId
            picked: tile.picked
            coverWidth: tile.coverWidth
        }

        // To the left of the check and never over it — the corner is taken, and a button does
        // not cover what it commands. On its own dark veil, because an illustration can be
        // pale and grey dots laid straight on one are invisible one time in three.
        CommandMenu {
            id: commands

            objectName: "commands-tile-" + tile.entryId
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.top: parent.top
            anchors.topMargin: 4
            coverWidth: tile.coverWidth
            visible: !tile.missing
            veiled: true
            seriesId: tile.seriesId
            entryId: tile.entryId
            fileName: tile.fileName
            label: tile.title
            chapters: tile.chapters
            onReimportAsked: tile.reimportAsked()
            onChapterAsked: tile.chapterAsked()
        }
    }

    Text {
        id: name

        anchors.left: frame.left
        anchors.right: tile.finished ? check.left : frame.right
        anchors.rightMargin: tile.finished ? 6 : 0
        anchors.top: frame.bottom
        anchors.topMargin: 6
        height: Math.min(implicitHeight, 34)
        textFormat: Text.PlainText
        text: tile.caption
        color: tile.missing ? Theme.inkFaint : Theme.ink
        font.family: Theme.displayFamily
        font.pixelSize: 13
        font.weight: Font.DemiBold
        lineHeightMode: Text.FixedHeight
        lineHeight: 17
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    Text {
        id: count

        objectName: "count-" + tile.entryId
        anchors.left: frame.left
        anchors.right: name.right
        anchors.top: name.bottom
        anchors.topMargin: 2
        height: 16
        textFormat: Text.PlainText
        text: tile.webtoon ? tile.readPercent : tile.pages
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: 11
        elide: Text.ElideRight
    }

    // At the right of the words, centred on the two lines they make.
    ReadCheck {
        id: check

        key: tile.entryId
        finished: tile.finished
        anchors.right: frame.right
        y: name.y + (count.y + count.height - name.y - height) / 2
    }
}
