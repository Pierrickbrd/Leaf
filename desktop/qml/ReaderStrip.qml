// A webtoon, as one strip: the pages edge to edge at the width of the window, scrolled.
//
// A webtoon is drawn to be read by scrolling, and cut at its pages it is read wrongly — a
// scene runs across a page boundary, and a turn that jumps a whole page loses the thread
// (spec §04). So a webtoon is *always* this and every other book *never* is: no pair, no
// sense of reading, no thirds.
//
// **A page's height is known before its image is.** The server measured it, and a delegate
// is sized from that, scaled to the strip's width; a delegate sized from its image instead is
// a page that is 0 pixels high until it arrives and jumps the whole strip when it does. A page
// the server could not measure gets a provisional 1.4 and takes the image's proportions when
// it arrives, which is the only case where the strip moves under the reader's eye.
//
// **Only the reader's own movement is reported.** Opening, and the scrub sending the strip to
// a page, position it with `positionViewAtIndex`, and the layout settles a few frames later;
// reported as scrolling, that settling recorded page 47 for a volume opened at 46, with
// nobody having touched anything. So `Reader` is told when a wheel, a drag, a key or the end
// of the strip moved it, and never otherwise.
//
// **A strip is the width of the window, or of its files.** « Largeur » draws every page edge
// to edge; « Original » draws each at the width of its own file, held to the window's and
// centred. « Hauteur » means nothing for a column with no end and is treated as « Largeur ».
// Either changes every page's height at once, and a strip left alone keeps its pixel offset
// while the pages above it shrink: the page being read slid out of the top. It is put back at
// the top (`settle`), unless a hand is on the strip.
//
// The images are the list's: a delegate is created as it comes within a screen (`cacheBuffer`)
// of the view and destroyed after, which cancels what had not arrived — a volume of two
// hundred long images is never all in memory.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

ListView {
    id: strip

    /// The page last reported to `Reader`, or placed from it. A change of `Reader.firstPage`
    /// that is not this one came from elsewhere (the scrub) and the strip goes there.
    property int reported: -1
    /// The reader moved it, as opposed to the strip placing itself.
    property bool user: false
    /// What each image is asked at: the window's width, or 0 for the file as it is — the
    /// reader's to say, once, for the pages and for what is fetched ahead.
    required property int requestWidth
    readonly property bool original: Preferences.fit === Preferences.Original
    /// A click, in the strip's own coordinates, which are the reader's.
    signal tapped(real x, real y)
    readonly property real screen: strip.height * 0.9

    /// A screen down, or up: by most of one, so the last lines read stay in view.
    function scrollScreen(direction) {
        const farthest = Math.max(0, strip.contentHeight - strip.height) + strip.originY
        const target = Math.max(strip.originY, Math.min(farthest,
                                                        strip.contentY + direction * strip.screen))
        strip.user = true
        glide.stop()
        glide.from = strip.contentY
        glide.to = target
        glide.start()
    }

    /// The wheel or the touchpad, in pixels. A touchpad sends pixels and is followed exactly;
    /// a notch is 120 eighths of a degree and moves as many pixels. By hand because the
    /// Flickable's own wheel handling moved nothing under the reader's window, and a strip
    /// that cannot be scrolled with a wheel is not a webtoon reader.
    function scrollBy(pixels) {
        glide.stop()
        strip.user = true
        const farthest = Math.max(0, strip.contentHeight - strip.height) + strip.originY
        strip.contentY = Math.max(strip.originY, Math.min(farthest, strip.contentY + pixels))
    }

    function toTheEnd(down) {
        glide.stop()
        strip.user = true
        if (down)
            strip.positionViewAtEnd()
        else
            strip.positionViewAtBeginning()
    }

    /// Puts the page `Reader` holds at the top of the view.
    function place() {
        if (Reader.firstPage < 0 || strip.count === 0)
            return
        glide.stop()
        strip.user = false
        strip.positionViewAtIndex(Reader.firstPage, ListView.Beginning)
        strip.reported = Reader.firstPage
    }

    /// The page being read, back at the top after a change that re-measured every page —
    /// the window's width, the fit — and left alone while the reader is moving the strip
    /// themselves, whose position is theirs.
    function settle() {
        if (strip.moving || glide.running)
            return
        Qt.callLater(strip.place)
    }

    /// The page across the middle of the view; the last one once the bottom is reached,
    /// because a last page too short to ever cross the middle would otherwise leave a
    /// finished volume unfinished.
    function report() {
        if (!strip.user || strip.count === 0)
            return
        const page = strip.atYEnd ? strip.count - 1
                                  : strip.indexAt(strip.width / 2,
                                                  strip.contentY + strip.height / 2)
        if (page < 0 || page === strip.reported)
            return
        strip.reported = page
        Reader.scrolledTo(page)
    }

    objectName: "reader-strip"
    model: Reader.webtoon && !Reader.loading && Reader.trouble.length === 0
           ? Reader.pageCount : 0
    visible: strip.count > 0
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    // One screen ahead (spec §10) — and one behind, which is what a list gives.
    cacheBuffer: strip.height

    onCountChanged: {
        strip.reported = -1
        Qt.callLater(strip.place)
    }
    onWidthChanged: strip.settle()
    onOriginalChanged: strip.settle()
    onContentYChanged: strip.report()
    onAtYEndChanged: strip.report()
    onMovementStarted: strip.user = true

    delegate: Item {
        id: slice

        required property int index

        // What the server measured, else the image's own, else a plausible strip.
        readonly property real known: Reader.pageRatio(slice.index)
        readonly property real ratio: slice.known > 0 ? slice.known
                                      : picture.implicitWidth > 0
                                        ? picture.implicitHeight / picture.implicitWidth : 1.4

        // A file whose width the server could not measure is drawn the window's.
        readonly property real fileWidth: Reader.pageNativeWidth(slice.index)
        readonly property real drawn: strip.original && slice.fileWidth > 0
                                      ? Math.min(strip.width, slice.fileWidth) : strip.width

        /// The image this slice last drew, kept under a copy asked at a new width — the
        /// page's own rule (`ReaderPage`): a strip whose pages went blank at every step of a
        /// window widened was a strip with holes in it for as long as they took to come back.
        property url kept: ""

        objectName: "strip-page-" + slice.index
        width: strip.width
        height: Math.round(slice.drawn * slice.ratio)

        Image {
            id: before

            objectName: "strip-before-" + slice.index
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.round(slice.drawn)
            height: parent.height
            source: slice.kept
            visible: picture.status !== Image.Ready && before.status === Image.Ready
            asynchronous: true
            fillMode: Image.Stretch
            smooth: true
            mipmap: true
        }

        Image {
            id: picture

            objectName: "strip-image-" + slice.index
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.round(slice.drawn)
            height: parent.height
            source: Reader.pageUrl(slice.index, strip.requestWidth)
            asynchronous: true
            fillMode: Image.Stretch
            smooth: true
            mipmap: true

            onStatusChanged: {
                if (picture.status === Image.Ready)
                    slice.kept = picture.source
            }
        }
    }

    // Followed, not fought: a page the scrub chose is the strip's to go to, and the page the
    // strip itself reported is already where it is.
    Connections {
        target: Reader

        function onChanged() {
            if (!Reader.loading && Reader.firstPage >= 0 && Reader.firstPage !== strip.reported)
                strip.place()
        }
    }

    // Here and not on the reader above: a Flickable keeps the press for itself in case it
    // becomes a drag, and a handler on its parent saw no click in the middle of a strip.
    TapHandler {
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: {
            const at = strip.mapFromItem(null, point.scenePosition)
            strip.tapped(at.x, at.y)
        }
    }

    NumberAnimation {
        id: glide
        // Named so that the tests can wait for it to stop, and say so when it is gone.
        objectName: "strip-glide"

        target: strip
        property: "contentY"
        // A screen down in one frame under « Sans effet », which moves nothing.
        duration: Preferences.motion === Preferences.Instant ? 0 : 180
        easing.type: Easing.OutCubic
    }
}
