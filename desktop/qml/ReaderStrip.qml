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
// **A strip is the width of its files, or of the window, times a zoom.** « Original », the
// default, draws each page at the width of its own file; « Largeur » draws every page edge to
// edge. The zoom (`Preferences.stripZoom`) multiplies either, and the column is never wider
// than the window: a strip cut by both edges is a strip read sideways. Its fit is its own, not
// the books': a webtoon at a laptop's window width drew its panels at twice the size they were
// drawn at (test of 24/11/2026). A change of fit changes every page's height at once, and a
// strip left alone keeps its pixel offset while the pages above it shrink: the page being read
// slid out of the top. It is put back at the top (`settle`), unless a hand is on the strip; a
// zoom keeps the point at the middle of the screen where it is (`keepTheMiddle`).
//
// **The arrows drive, they do not jump.** A held arrow scrolls at a cruising speed reached in
// a few frames and left as quickly, frame by frame (`drive`): a screen at a time, glided, read
// as jolts — the test of 24/11/2026 — and the key's own repeat, a jump every 30 ms after a
// half-second wait, is a stutter. The space bar and the page keys still move by a screen.
//
// The images are the list's: a delegate is created as it comes within three screens
// (`cacheBuffer`) of the view and destroyed after, which cancels what had not arrived — a
// volume of two hundred long images is never all in memory. One screen ahead was a strip
// whose next page arrived as it came into view.

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
    readonly property bool original: Preferences.stripFit === Preferences.Original
    /// The zoom drawn: the one being pinched while fingers are on the pad, else the kept one.
    /// A pinch writes the preference once, when it ends, not at every frame of it.
    property real pinched: 0
    readonly property real zoom: strip.pinched > 0 ? strip.pinched : Preferences.stripZoom

    /// Which way an arrow is held: 1 down, −1 up, 0 none. See `drive`.
    property int held: 0
    /// Pixels a second, signed, as the strip is moving under a held arrow.
    property real speed: 0
    /// Most of a screen a second: a panel is read on its way through without the eyes
    /// chasing it, and a chapter is crossed in a few seconds.
    readonly property real cruise: Math.max(480, strip.height * 0.85)
    /// A click, in the strip's own coordinates, which are the reader's.
    signal tapped(real x, real y)
    /// The wheel or the touchpad over the strip, for the reader to scroll or zoom with.
    signal wheeled(var event, real x, real y)
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

    /// An arrow pressed: the strip starts moving that way and keeps on while it is held.
    function press(direction) {
        glide.stop()
        strip.user = true
        strip.held = direction
        tap.letGo = false
        tap.restart()
    }

    /// An arrow let go — the one held, not one pressed and released under it. Let go before
    /// `tap` is over, it is held until then: a press and its release arrive within one frame
    /// of a key tapped, and a strip that only moves while a key is down did not move at all.
    function release(direction) {
        if (strip.held !== direction)
            return
        if (tap.running)
            tap.letGo = true
        else
            strip.held = 0
    }

    /// One frame of a held arrow: towards the cruising speed while it is held, back to rest
    /// once it is not, in about a sixth of a second either way; « Sans effet » goes straight to
    /// either. Stopped where the strip stops.
    function drive(seconds) {
        const aimed = strip.held * strip.cruise
        const instant = Preferences.motion === Preferences.Instant
        const step = instant ? Infinity : strip.cruise / 0.16 * seconds
        const gap = aimed - strip.speed
        strip.speed = Math.abs(gap) <= step ? aimed : strip.speed + Math.sign(gap) * step
        if (strip.speed === 0)
            return
        const farthest = Math.max(0, strip.contentHeight - strip.height) + strip.originY
        const to = Math.max(strip.originY,
                            Math.min(farthest, strip.contentY + strip.speed * seconds))
        if (to === strip.contentY)
            strip.speed = 0
        strip.contentY = to
    }

    /// The zoom at `level`, the middle of the screen kept where it is. `kept` writes it to the
    /// preferences; a pinch under way leaves it unwritten until the fingers lift.
    function zoomTo(level, kept) {
        const to = Math.max(1 / 3, Math.min(3, level))
        strip.keepTheMiddle(() => {
            if (kept) {
                strip.pinched = 0
                Preferences.chooseStrip(Preferences.stripFit, to)
            } else {
                strip.pinched = to
            }
        })
    }

    /// A step of a tenth up (1) or down (−1), as the keys and the wheel give.
    function stepZoom(direction) {
        strip.zoomTo(strip.zoom * (direction > 0 ? 1.1 : 1 / 1.1), true)
    }

    /// Runs `change`, which re-measures the pages, and puts the point that was at the middle of
    /// the screen back there: the page being read and the line in it, not the pixel offset,
    /// which a zoom moves to a different page altogether.
    function keepTheMiddle(change) {
        const middle = strip.contentY + strip.height / 2
        const index = strip.indexAt(strip.width / 2, middle)
        const before = index >= 0 ? strip.itemAtIndex(index) : null
        const fraction = before && before.height > 0 ? (middle - before.y) / before.height : 0
        change()
        if (index < 0)
            return
        strip.forceLayout()
        strip.positionViewAtIndex(index, ListView.Beginning)
        const after = strip.itemAtIndex(index)
        if (!after)
            return
        const farthest = Math.max(0, strip.contentHeight - strip.height) + strip.originY
        strip.contentY = Math.max(strip.originY, Math.min(farthest, after.y
                                                          + fraction * after.height
                                                          - strip.height / 2))
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
    // Three screens ahead (spec §10 asks one) — and three behind, which is what a list gives.
    cacheBuffer: strip.height * 3

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
        readonly property real drawn: Math.min(strip.width,
                                               (strip.original && slice.fileWidth > 0
                                                ? slice.fileWidth : strip.width) * strip.zoom)

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

    // The wheel, taken before the list's own: a handler on an item hears the event before
    // the item does. Under Qt 6.10 the Flickable began scrolling by itself, so the reader's
    // handlers below never heard a notch — Ctrl and the wheel zoomed nothing, and a notch
    // scrolled by the list's measure instead of the reader's.
    WheelHandler {
        id: wheel

        target: null
        onWheel: event => {
            const at = strip.mapFromItem(null, wheel.point.scenePosition)
            strip.wheeled(event, at.x, at.y)
        }
    }

    // The shortest hold: a tapped arrow drives this long, and comes a short way — about a
    // tenth of a screen, a few lines of a panel.
    Timer {
        id: tap

        property bool letGo: false

        interval: 120
        onTriggered: {
            if (tap.letGo && strip.held !== 0)
                strip.held = 0
            tap.letGo = false
        }
    }

    // A held arrow, frame by frame, and the rest it comes to after: see `drive`.
    FrameAnimation {
        objectName: "strip-drive"
        running: strip.held !== 0 || strip.speed !== 0
        onTriggered: strip.drive(Math.min(frameTime, 0.05))
    }

    // An arrow held while the window loses the keys never sees its release: the strip would
    // run to the end of the volume on its own.
    readonly property bool keysHere: Window.active
    onKeysHereChanged: if (!strip.keysHere) strip.held = 0

    // A pinch on the touchpad, about the middle: drawn as the fingers move, kept when they
    // lift. Not on books' terms (`ReaderView`): a strip has no page to pan across.
    PinchHandler {
        id: pinch

        property real from: 1

        target: null
        onActiveChanged: {
            if (pinch.active)
                pinch.from = strip.zoom
            else if (strip.pinched > 0)
                strip.zoomTo(strip.pinched, true)
        }
        onActiveScaleChanged: {
            if (pinch.active)
                strip.zoomTo(pinch.from * pinch.activeScale, false)
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
