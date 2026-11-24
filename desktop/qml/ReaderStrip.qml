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
// **A strip is drawn at a width its series keeps, and never wider than its files.** The series
// keeps a width (`Reader.stripWidth`), not a factor: a series mixes chapters scanned at 720
// and at 1080 pixels, and one factor drew them at two widths. A chapter is drawn at that width
// from the width most of its pages are (`Reader.commonWidth`), each page in proportion, and
// never past that width counted in the screen's own pixels: at a scale of 1,3333 a file of
// 720 is 540 wide, and drawn at 720 it was blown up by a third. So a chapter of 720 between
// chapters of 1080 stays narrower and sharp; zoomed for good, it keeps a width of its own
// (`Reader.zoomStrip`, the rule). It opens the first time at its files' width — a webtoon at
// a laptop's window width drew its panels at twice the size they were drawn at (test of
// 24/11/2026) — and that width is its 100 %. The column is never wider than the window: a
// strip cut by both edges is a strip read sideways, and a narrower window changes nothing
// kept. A zoom changes every page's height at once and keeps the point at the middle of the
// screen where it is (`keepTheMiddle`); a narrower window puts the page being read back at the
// top (`settle`), since a strip left alone kept its pixel offset and the page slid out of it.
//
// **A pinch is a magnifier, and keeps nothing.** The fingers enlarge what is under them, the
// strip goes on scrolling under the glass and moves sideways with two fingers; pinching back,
// a double click, Escape or another chapter put it away, and the bar at the foot says so
// while it is there. A lasting zoom is Ctrl and the wheel, Ctrl + and −, Ctrl 0 back.
//
// **The A reads by itself**: the strip goes down at a steady speed until a hand moves it —
// the wheel, an arrow, a drag — or its foot is reached (`autoRead`). And going down by hand,
// the reader is reading: the bars go (`wentDown`), never going back up.
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
    /// The screen's pixels in one of the strip's: what a file's width is divided by to be
    /// drawn at its own size.
    readonly property real density: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    /// This chapter's files on this screen at their own size; the window when unmeasured.
    readonly property real ownWidth: Reader.commonWidth > 0 ? Reader.commonWidth / strip.density
                                                          : strip.width
    /// The width meant for this chapter, before the window has its say: its own, else the
    /// series' but never past its files', else its files' until the series keeps one.
    readonly property real meant: Reader.stripOwn ? Reader.stripWidth
                                  : Reader.stripWidth > 0 ? Math.min(Reader.stripWidth,
                                                                     strip.ownWidth)
                                                          : strip.ownWidth
    /// The width the chapter is drawn at, held to the window.
    readonly property real chapterWidth: Math.max(1, Math.min(strip.width, strip.meant))
    /// What the bar at the foot says: the chapter on screen against the series' 100 %.
    readonly property real level: Reader.stripBase > 0 ? strip.chapterWidth / Reader.stripBase
                                                       : 1
    /// The pinch's magnifier: 1 is none. What is under the glass is the strip scaled by
    /// `loupe` and shifted by `loupeX`, `loupeY`, in the reader's coordinates.
    property real loupe: 1
    property real loupeX: 0
    property real loupeY: 0
    /// A lasting zoom was made, for the bar at the foot to say the level.
    signal zoomed()
    /// The reader moved the strip down by hand: they are reading, and the bars go.
    signal wentDown()
    /// The A: the strip goes down by itself, `autoSpeed` pixels a second.
    property bool autoRead: false
    /// From a twenty-fifth of a screen a second to four tenths, in eleven steps each a
    /// quarter faster than the last (`Preferences.stripAutoSpeed`): the slowest reads a dense
    /// page, the fastest crosses a fight scene.
    /// Steps 0 to 10, the middle one 5 (`Preferences::LastStep`, `MiddleStep`, which a
    /// `constexpr` does not take to QML).
    readonly property real autoSpeed: strip.height * 0.04
                                      * Math.pow(10, Preferences.stripAutoSpeed / 10)
    /// How far a notch or a stroke moves the strip, in eleven steps
    /// (`Preferences.stripSensitivity`) each a fifth farther than the last: from the measure
    /// of a notch to six times it. Twelve times was tried, and swept a pad's two fingers past
    /// everything at once; the first measure at three quarters was slower than a hand reads.
    readonly property real sensitivity: Math.pow(6, Preferences.stripSensitivity / 10)
    /// Pixels a second the strip goes on at after two fingers left the pad, signed.
    property real coasting: 0
    /// The last strokes of the pad, for the speed it goes on at: `[seconds, pixels]`.
    property var strokes: []
    /// How far down the chapter the view is, 0 at the top and 1 at the foot.
    readonly property real fraction: {
        const room = strip.contentHeight - strip.height
        return room > 0 ? Math.max(0, Math.min(1, (strip.contentY - strip.originY) / room)) : 1
    }

    /// A hand on the strip: whatever reads by itself stops, and whatever was still coasting.
    function handOn(down) {
        glide.stop()
        strip.autoRead = false
        strip.coasting = 0
        strip.user = true
        if (down)
            strip.wentDown()
    }

    /// The mark on the right edge dragged or clicked: that far down the chapter.
    function jumpTo(fraction) {
        strip.handOn(false)
        const room = Math.max(0, strip.contentHeight - strip.height)
        strip.contentY = strip.originY + Math.max(0, Math.min(1, fraction)) * room
    }

    /// Which way an arrow is held: 1 down, −1 up, 0 none. See `drive`.
    property int held: 0
    /// Pixels a second, signed, as the strip is moving under a held arrow.
    property real speed: 0
    /// Most of a screen a second: a panel is read on its way through without the eyes
    /// chasing it, and a chapter is crossed in a few seconds.
    readonly property real cruise: Math.max(480, strip.height * 0.85)
    /// A click, in the strip's own coordinates, which are the reader's.
    signal tapped(real x, real y)
    readonly property real screen: strip.height * 0.9

    /// A screen down, or up: by most of one, so the last lines read stay in view.
    function scrollScreen(direction) {
        const farthest = Math.max(0, strip.contentHeight - strip.height) + strip.originY
        const target = Math.max(strip.originY, Math.min(farthest,
                                                        strip.contentY + direction * strip.screen))
        strip.handOn(direction > 0)
        glide.from = strip.contentY
        glide.to = target
        glide.start()
    }

    /// The wheel, the touchpad or a drag, in pixels, already multiplied by the sensitivity. A
    /// touchpad sends pixels and is followed exactly; a notch is 120 eighths of a degree and
    /// moves as many pixels. By hand because the Flickable's own handling of them follows the
    /// hand at its own measure and knows nothing of the settings.
    function scrollBy(pixels) {
        strip.handOn(pixels > 0)
        const farthest = Math.max(0, strip.contentHeight - strip.height) + strip.originY
        strip.contentY = Math.max(strip.originY, Math.min(farthest, strip.contentY + pixels))
    }

    /// A stroke of two fingers on the pad, `pixels` down: followed, and remembered for the
    /// speed the strip goes on at once they lift (`coast`).
    function stroke(pixels) {
        const now = Date.now() / 1000
        strip.strokes = strip.strokes.filter(one => now - one[0] < 0.1).concat([[now, pixels]])
        strip.scrollBy(pixels)
    }

    /// The fingers lifted: the strip goes on at the speed of their last tenth of a second and
    /// slows to rest, as a page pushed on a desk — a desktop that sends no momentum of its own
    /// left the strip dead the instant they lifted. Not under « Sans effet », which moves nothing
    /// by itself.
    function coast() {
        const now = Date.now() / 1000
        const recent = strip.strokes.filter(one => now - one[0] < 0.1)
        strip.strokes = []
        if (recent.length < 2 || Preferences.motion === Preferences.Instant)
            return
        const span = Math.max(0.016, now - recent[0][0])
        const pixels = recent.reduce((sum, one) => sum + one[1], 0)
        const speed = pixels / span
        if (Math.abs(speed) > 120)
            strip.coasting = speed
    }

    /// An arrow pressed: the strip starts moving that way and keeps on while it is held.
    function press(direction) {
        strip.handOn(direction > 0)
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

    /// A lasting zoom by `factor`, the middle of the screen kept where it is. Never wider than
    /// the window: a step past it would keep a width nobody sees.
    function zoomBy(factor) {
        const to = Math.min(strip.width, strip.chapterWidth * factor)
        if (Math.abs(to - strip.chapterWidth) < 0.5)
            return
        const from = strip.chapterWidth
        strip.keepTheMiddle(() => Reader.zoomStrip(from, to, strip.ownWidth))
        strip.zoomed()
    }

    /// A step of a tenth up (1) or down (−1), as the keys and the wheel give.
    function stepZoom(direction) {
        strip.zoomBy(direction > 0 ? 1.1 : 1 / 1.1)
    }

    /// Ctrl 0: the chapter's own width forgotten, or the series back at its 100 %.
    function resetZoom() {
        strip.keepTheMiddle(() => Reader.resetStrip())
        strip.zoomed()
    }

    /// The glass at `scale`, the point at (`x`, `y`) of the reader staying under the fingers,
    /// then held so that the strip still covers the whole view: no edge of nothing shows.
    function magnify(scale, x, y) {
        const to = Math.max(1, Math.min(4, scale))
        const grown = to / strip.loupe
        strip.loupeX = x - (x - strip.loupeX) * grown
        strip.loupeY = y - (y - strip.loupeY) * grown
        strip.loupe = to
        strip.holdTheGlass()
    }

    /// The glass moved by (`dx`, `dy`): sideways it moves over the strip, down or up the strip
    /// scrolls under it — a column under a glass is still read by scrolling.
    function moveTheGlass(dx, dy) {
        strip.loupeX += dx
        strip.holdTheGlass()
        if (dy !== 0)
            strip.scrollBy(-dy / strip.loupe)
    }

    function holdTheGlass() {
        strip.loupeX = Math.max(strip.width * (1 - strip.loupe), Math.min(0, strip.loupeX))
        strip.loupeY = Math.max(strip.height * (1 - strip.loupe), Math.min(0, strip.loupeY))
    }

    /// The glass put away; true when there was one, for Escape to know it did something.
    function closeLoupe() {
        if (strip.loupe === 1)
            return false
        strip.loupe = 1
        strip.loupeX = 0
        strip.loupeY = 0
        return true
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
        strip.handOn(down)
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
        if (strip.moving || strip.dragged || glide.running)
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
    // Nothing here is moved by the list itself: its own drag and its own wheel followed the
    // hand one for one and ignored the sensitivity, which two fingers on a touchpad and a
    // click held and dragged both went round. The wheel (`ReaderView`), the drag below and the
    // arrows all move it through `scrollBy`, so that one setting is the distance a hand makes.
    interactive: false
    // Three screens ahead (spec §10 asks one) — and three behind, which is what a list gives.
    cacheBuffer: strip.height * 3

    onCountChanged: {
        strip.reported = -1
        strip.autoRead = false
        strip.closeLoupe()
        // A series opened here for the first time keeps the width it opens at: its files',
        // held to the window.
        if (strip.count > 0)
            Reader.startStrip(Math.min(strip.ownWidth, strip.width))
        Qt.callLater(strip.place)
    }
    onWidthChanged: {
        strip.settle()
        strip.holdTheGlass()
    }
    onContentYChanged: strip.report()
    onAtYEndChanged: strip.report()
    onMovementStarted: strip.handOn(false)
    onAutoReadChanged: {
        if (!strip.autoRead)
            return
        glide.stop()
        strip.user = true
    }

    delegate: Item {
        id: slice

        required property int index

        // What the server measured, else the image's own, else a plausible strip.
        readonly property real known: Reader.pageRatio(slice.index)
        readonly property real ratio: slice.known > 0 ? slice.known
                                      : picture.implicitWidth > 0
                                        ? picture.implicitHeight / picture.implicitWidth : 1.4

        // In proportion to the chapter's common width: a narrower credits page stays narrower,
        // a double page wider. A file the server could not measure is the chapter's width.
        readonly property real fileWidth: Reader.pageNativeWidth(slice.index)
        readonly property real drawn: Math.min(strip.width,
                                               slice.fileWidth > 0 && Reader.commonWidth > 0
                                               ? slice.fileWidth * strip.chapterWidth
                                                 / Reader.commonWidth
                                               : strip.chapterWidth)

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
        // The second click of a double click puts the glass away; the two clicks call the
        // bars and send them back, which leaves them as they were.
        onDoubleTapped: strip.closeLoupe()
        onTapped: {
            const at = strip.mapFromItem(null, point.scenePosition)
            strip.tapped(at.x, at.y)
        }
    }

    /// A press held and dragged, with a mouse, a touchpad's click or a finger on a screen: the
    /// strip follows the hand one for one at the middle step, and by the curve of the settings
    /// either side, but only by its sixth root and a half: a hand that holds the page feels
    /// the page, and the full range went from a strip that hardly moved to one nobody held.
    /// It goes on a little after the release as the touchpad's swipe does (`coast`).
    readonly property bool dragged: drag.active
    DragHandler {
        id: drag

        property real last: 0

        target: null
        xAxis.enabled: false
        grabPermissions: PointerHandler.CanTakeOverFromAnything
        onActiveChanged: {
            if (active)
                drag.last = 0
            else
                strip.coast()
        }
        onActiveTranslationChanged: {
            if (!drag.active)
                return
            const moved = drag.activeTranslation.y - drag.last
            drag.last = drag.activeTranslation.y
            strip.stroke(-moved * Math.pow(strip.sensitivity / strip.handMeasure, 0.6))
        }
    }

    /// The sensitivity at which a dragged strip follows the hand one for one: the middle step.
    readonly property real handMeasure: Math.pow(6, 0.5)

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

    // What the pad left the strip with, frame by frame: a third of its speed lost every tenth
    // of a second, until it is too slow to see or the strip stops.
    FrameAnimation {
        objectName: "strip-coast"
        running: strip.coasting !== 0
        onTriggered: {
            const seconds = Math.min(frameTime, 0.05)
            const farthest = Math.max(0, strip.contentHeight - strip.height) + strip.originY
            const to = Math.max(strip.originY,
                                Math.min(farthest, strip.contentY + strip.coasting * seconds))
            strip.coasting *= Math.pow(0.67, seconds / 0.1)
            if (to === strip.contentY || Math.abs(strip.coasting) < 20)
                strip.coasting = 0
            strip.contentY = to
        }
    }

    // The A, frame by frame, until the foot.
    FrameAnimation {
        objectName: "strip-auto"
        running: strip.autoRead
        onTriggered: {
            const farthest = Math.max(0, strip.contentHeight - strip.height) + strip.originY
            const to = Math.min(farthest,
                                strip.contentY + strip.autoSpeed * Math.min(frameTime, 0.05))
            if (to <= strip.contentY) {
                strip.autoRead = false
                return
            }
            strip.contentY = to
        }
    }

    // An arrow held while the window loses the keys never sees its release: the strip would
    // run to the end of the volume on its own.
    readonly property bool keysHere: Window.active
    onKeysHereChanged: if (!strip.keysHere) strip.held = 0

    // The glass, under the fingers: see the head of this file. In the reader's coordinates,
    // which the glass does not move, and not the strip's, which it does.
    PinchHandler {
        id: pinch

        property real from: 1
        property point last

        function under() {
            return strip.parent.mapFromItem(null, pinch.centroid.scenePosition)
        }

        target: null
        onActiveChanged: {
            if (pinch.active) {
                pinch.from = strip.loupe
                pinch.last = pinch.under()
            } else if (strip.loupe < 1.05) {
                // Pinched back to nearly nothing: nothing, rather than a glass of 1.02.
                strip.closeLoupe()
            }
        }
        onActiveScaleChanged: {
            if (!pinch.active)
                return
            const at = pinch.under()
            strip.magnify(pinch.from * pinch.activeScale, at.x, at.y)
        }
        onCentroidChanged: {
            if (!pinch.active || strip.loupe === 1)
                return
            const at = pinch.under()
            strip.moveTheGlass(at.x - pinch.last.x, 0)
            strip.loupeY += at.y - pinch.last.y
            strip.holdTheGlass()
            pinch.last = at
        }
    }

    transform: [
        Scale {
            xScale: strip.loupe
            yScale: strip.loupe
        },
        Translate {
            x: strip.loupeX
            y: strip.loupeY
        }
    ]

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
