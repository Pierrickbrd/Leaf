// One page of the book, at the size its fit gives it, with a hairline round it.
//
// The hairline is the spec's (*L'architecture du client*, §08): a white page on the reader's
// paper has an edge of its own, but a page that is white to its border on a dark paper — or a
// dark page on the light one — runs into the paper without it, and a pair reads as one wide
// image.
//
// Sized from the image's own proportions and never from a fixed 2:3. A book is not always
// that shape, and a page drawn into a box of the wrong one leaves bands of paper inside the
// hairline, which is a frame round the page rather than its edge.

import QtQuick
import Leaf

Item {
    id: leaf

    /// From 0, as `Reader` counts.
    required property int page
    /// What its items are named after: the page on screen is `reader-page`, the copy that
    /// leaves with a turn `reader-ghost` (`ReaderView`), so a test finds the one it means.
    property string name: "reader-page"
    /// The room this page has: the stage, or half of it when it stands in a pair.
    required property real slotWidth
    required property real slotHeight
    /// What the image is asked at — `Reader.askedWidth`, or 0 for the file as it is.
    required property int requestWidth
    /// How far the reader has enlarged the page. The page is laid out whole and scaled by the
    /// view; this only says when the copy fetched for the screen is no longer enough.
    property real zoom: 1

    readonly property bool loaded: picture.status === Image.Ready
    /// The image this page last drew, and which page it was: what stays on screen while the
    /// same page is fetched at another width.
    property url kept: ""
    property int keptPage: -1
    /// The same page asked at a new width and not there yet: the copy drawn before stands in.
    readonly property bool bridged: !leaf.loaded && leaf.keptPage === leaf.page
                                    && before.status === Image.Ready
    /// Height over width. Before the image arrives the one it replaces, else the usual 2:3, so
    /// the frame does not jump for the common case.
    readonly property real ratio: picture.implicitWidth > 0
                                  ? picture.implicitHeight / picture.implicitWidth
                                  : leaf.bridged && before.implicitWidth > 0
                                    ? before.implicitHeight / before.implicitWidth : 1.5

    // Height is the whole page, as the spec draws a reader at rest: it fits the height and,
    // should the window be narrower than the page is tall, the width — never a page cut.
    // Width fills the width and lets a tall page run off the bottom, for whoever reads text
    // too small at height; Original is the file's own pixels. What runs off is the zoom's
    // to move, not this.
    readonly property real drawnWidth: {
        switch (Preferences.fit) {
        case Preferences.Width:
            return leaf.slotWidth
        case Preferences.Original:
            return picture.implicitWidth > 0 ? picture.implicitWidth : leaf.slotWidth
        default:
            return Math.min(leaf.slotWidth, leaf.slotHeight / leaf.ratio)
        }
    }

    objectName: leaf.name + "-" + leaf.page
    width: Math.max(0, Math.round(leaf.drawnWidth))
    height: Math.max(0, Math.round(leaf.drawnWidth * leaf.ratio))

    // The copy drawn before, under the one being fetched, until that one is decoded: an
    // `Image` whose source changes shows nothing in between, and a window going full screen
    // blanked every page on it for as long as the new copy took. Only for the same page — a
    // page turned shows the next one or nothing, never the last one in its place. The file
    // is the one `picture` already holds, so this costs no request and no second decoding.
    Image {
        id: before

        objectName: leaf.name + "-before-" + leaf.page
        anchors.fill: parent
        source: leaf.kept
        visible: leaf.bridged
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }

    Image {
        id: picture

        objectName: leaf.name + "-image-" + leaf.page
        anchors.fill: parent
        // Read through `entryId` so the binding follows the book: page 3 of the next volume
        // is the same index and another file.
        source: Reader.entryId.length > 0 ? Reader.pageUrl(leaf.page, leaf.requestWidth) : ""
        // Decoded off the scene's thread: a page of a few megabytes decoded on it is a turn
        // that stutters.
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true

        onStatusChanged: {
            if (picture.status === Image.Ready) {
                leaf.kept = picture.source
                leaf.keptPage = leaf.page
            }
        }
    }

    // The file as it is, over the copy made for the screen, once the zoom draws the page
    // wider than that copy: scaled past its own pixels, the screen's copy is a blur, and a
    // page is zoomed precisely to read what is small. Asked once — the same file serves every
    // level up to 400 % — rather than at each level's width, which would be a request and a
    // resize on the server per step. The screen's copy stays drawn underneath until the file
    // arrives, so the page never blinks out: an `Image` whose source changes shows nothing
    // until the new one is decoded.
    Image {
        id: sharp

        readonly property bool wanted: leaf.requestWidth > 0
                                       && leaf.width * leaf.zoom > leaf.requestWidth

        objectName: leaf.name + "-sharp-" + leaf.page
        anchors.fill: parent
        source: sharp.wanted && Reader.entryId.length > 0 ? Reader.pageUrl(leaf.page, 0) : ""
        visible: sharp.status === Image.Ready
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -1
        color: "transparent"
        border.color: Theme.rule
        border.width: 1
    }
}
