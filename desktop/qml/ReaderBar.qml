// The reader's top bar: the way back, what is being read, and the four things to do with it.
//
// The application's bar carried over — its height, its surface, its hairline — so the reader
// is the same application with the paper changed, and not a second one. Round buttons of
// twenty-six, the size of the others the application draws on a card, because these sit
// over a page and not over a shelf.
//
// It follows the window as the dock does (`Widths.scale`), its buttons, its two lines and its
// height together: in full screen on a window of 1920, tried by hand on 22/10/2026, a bar of
// twenty-six-pixel buttons over a page that filled the screen was hard to find, and on a
// small window its words kept their drawn size over a page that had shrunk. Its words stop at
// their floors and never grow past their drawn size, its gaps grow twice as fast as the rest
// (23/10/2026, see `Scale`); in the narrow band it drops the keyboard's button rather than
// shrink further.
//
// It says nothing of its own: the book's name and place come from `Reader`, the words from
// `ReaderCaptions`, and what a button does is the screen's to decide — it only says it was
// pressed.

import QtQuick
import QtQuick.Layouts
import Leaf

Rectangle {
    id: bar

    property bool fullScreen: false
    /// The settings card is open under `tune`, which stays lit for as long as it is.
    property bool settingsOpen: false
    /// The volume's panel is open, and the book stays lit for as long as it is.
    property bool volumeOpen: false
    /// The shortcuts card is up, and the keyboard stays lit for as long as it is.
    property bool shortcutsOpen: false
    /// Where `tune` ends, in this bar's coordinates: the card hangs from it.
    readonly property real settingsRight: row.x + settings.x + settings.width

    signal backRequested()
    signal fullScreenToggled()
    signal settingsToggled()
    signal volumeToggled()
    signal shortcutsToggled()

    /// A round button that the keyboard can reach and that says what it does under the
    /// pointer. `IconButton` is drawn on covers, where neither is wanted; here both are, and
    /// they are written once for the six of them.
    component Stop: IconButton {
        id: stop

        // The layout's size and not only the item's: a row lays a child out at what it was
        // told it prefers, and a width alone was taken once and never again.
        width: Widths.scale.size(26)
        height: stop.width
        Layout.preferredWidth: stop.width
        Layout.preferredHeight: stop.width
        side: Widths.scale.icon(20)

        activeFocusOnTab: true
        Accessible.focusable: true
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                    || event.key === Qt.Key_Enter) {
                stop.press()
                event.accepted = true
            }
        }

        FocusRing {
            cornerRadius: stop.radius
        }

        LeafToolTip {
            visible: stop.hovered
            text: stop.label
        }
    }

    /// The chapter the first page on screen belongs to, `null` when none says where it
    /// begins. Read through `firstPage` and `chapters`, so the line follows a turn.
    readonly property var chapter: {
        const at = Reader.firstPage >= 0 ? Reader.chapterAt(Reader.firstPage) : -1
        return at >= 0 && at < Reader.chapters.length ? Reader.chapters[at] : null
    }

    objectName: "reader-bar"
    height: Widths.scale.size(58)
    color: Theme.surface

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.rule
        opacity: Theme.dark ? 0.7 : 0.45
    }

    RowLayout {
        id: row

        anchors.fill: parent
        anchors.leftMargin: Widths.shelfMargin
        anchors.rightMargin: Widths.shelfMargin
        spacing: Widths.scale.room(9)

        Stop {
            objectName: "reader-back"
            glyph: "arrow_back"
            label: Navigation.backLabel
            onTriggered: bar.backRequested()
        }

        Column {
            Layout.fillWidth: true
            Layout.leftMargin: Widths.scale.room(4)
            spacing: 1

            Text {
                objectName: "reader-title"
                width: parent.width
                text: Reader.seriesName
                // The series' name is the library's, not markup: « <b>x</b> » in a file name is
                // shown as written and never made bold, nor an `&amp;` turned into `&`.
                textFormat: Text.PlainText
                color: Theme.ink
                font.family: Theme.displayFamily
                font.pixelSize: Widths.scale.title(19)
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            Text {
                objectName: "reader-where"
                width: parent.width
                text: ReaderCaptions.where(Reader.entryLabel,
                                           bar.chapter ? bar.chapter.label : "",
                                           bar.chapter ? bar.chapter.title : "")
                // A chapter's label and title are the server's, read from the files.
                textFormat: Text.PlainText
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                elide: Text.ElideRight
            }
        }

        // The keys, beside the book: both open something to read about what is open, and
        // neither changes it. A Tabler keyboard, the card's own mark.
        Stop {
            objectName: "reader-shortcuts-button"
            // Not in the narrow band, where the bar's title had no room left beside six
            // buttons: a window that narrow is seldom read with a keyboard, and ? and F1 still
            // open the card.
            visible: Widths.band !== Widths.Band.Narrow
            glyph: "tabler-keyboard"
            label: ReaderCaptions.shortcuts
            held: bar.shortcutsOpen
            onTriggered: bar.shortcutsToggled()
        }

        Stop {
            objectName: "reader-volume-button"
            glyph: "menu_book"
            label: ReaderCaptions.volumePanel
            held: bar.volumeOpen
            onTriggered: bar.volumeToggled()
        }

        Stop {
            id: settings

            objectName: "reader-settings-button"
            glyph: "tune"
            label: ReaderCaptions.settings
            held: bar.settingsOpen
            onTriggered: bar.settingsToggled()
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: Widths.scale.size(20)
            Layout.leftMargin: Widths.scale.room(2)
            Layout.rightMargin: Widths.scale.room(2)
            color: Theme.rule
        }

        Stop {
            objectName: "reader-fullscreen"
            glyph: bar.fullScreen ? "fullscreen_exit" : "fullscreen"
            label: bar.fullScreen ? ReaderCaptions.leaveFullScreen : ReaderCaptions.fullScreen
            onTriggered: bar.fullScreenToggled()
        }
    }
}
