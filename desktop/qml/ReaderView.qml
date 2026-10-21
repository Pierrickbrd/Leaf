// The reader: the pages on their own paper, and the commands when they are called for.
//
// At rest there is nothing but the pages — no bar, no button, no number, and after two seconds
// without moving, no pointer (spec §01). A click in the middle calls both bars and a second
// sends them away; so does turning a page, because the page is what the reader came back to.
// A volume opens with both bars up, for three seconds without input or until the first turn:
// tried by hand on 22/10/2026, a reader opening a book found a bare page and nothing saying
// that the commands were a click away.
//
// **The thirds turn the page the way it lies, not the way it is named.** In a manga the page
// read next is on the left, so the left third goes forward, and so does `←`; in a book read
// left to right it is the right third and `→` (*L'architecture du client*, §11). `Reader`
// speaks only of reading forward and back, and this file is where a side becomes a sense. A
// book read downwards is turned by the right side like a left-to-right one, and `↓` goes on.
// The wheel goes down to go on whatever the side, since a wheel has no left or right.
//
// **A zoomed page is moved, a whole page is turned** (spec §09). Whatever runs off the screen
// — a page enlarged, or a tall page fitted to the width — is moved by the arrows, the space
// bar, the wheel and a drag until its edge is in view, and only the next press turns it. The
// zoom is of the screen and not of one page: a pair is a spread, and the half of a picture
// that crosses the spine is enlarged with the other half. Each page turned starts whole.
//
// **A turn slides, a jump fades** — tried by hand on 22/10/2026, a page replaced in one frame
// read as a flash. A turn (a key, a third, the wheel, a button) brings the new screen in from
// the side reading goes towards — from the right going on in a book read left to right, from
// the left in a manga, from below in a book read downwards — in 200 ms, while the screen left
// goes out the other way; a jump (the progress bar, a chapter, a volume opened) fades across
// in 150 ms. The screen left is drawn by `ghost`, whose images are the ones that were on
// screen and come from the image cache, never from the server; and the new screen waits for
// its images, the old one standing, so no frame is ever bare paper. Nothing moves while the
// page is zoomed or dragged, nor in a webtoon; and a turn made while one is running ends that
// one at once, so a run of turns never lags behind the hand.
//
// **That is « Glisser », one of three** (`Preferences.motion`, owner's choice of 22/10/2026):
// « Fondu » fades a turn as it fades a jump, and « Sans effet » moves nothing at all — not
// the pages, not the bars, the dock or the cards coming and going. « Sans effet » still waits
// for the images: the screen that comes is hidden, the old one standing, until they are there,
// and is then put up in one frame. A page changed with no motion is not a page put on bare paper.
//
// **The paper is the reader's too** (`Preferences.backdrop`): what is behind the pages, the
// strip, the margins and the gaps of a turn is one rectangle, so a fade or a wait shows the
// colour chosen and never another — no black frame across a white backdrop. The bars, the dock
// and the cards keep the application's colours: they are the application, not the book.
//
// The book itself is `Reader`'s: which pages are on screen, what was recorded, what comes
// next. This file draws them and turns them.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

FocusScope {
    id: view

    /// The two bars, called by a click in the middle.
    property bool barsShown: false
    /// The settings card is open under the bar. It lives only while the bars do: they go with
    /// a turned page, and a card left hanging over a bare page would be a menu with no button.
    property bool settingsOpen: false
    /// The volume's panel is open, a card over the right of the pages. Unlike the settings
    /// card it does not die with the bars: it is something to read beside the book, and a
    /// turned page is not a reason to take it away.
    property bool volumeOpen: false
    /// The card of the reader's keys is up, centred over the pages. One of the three things
    /// hung over the book, and never with either of the others; never over the end card.
    property bool shortcutsOpen: false
    /// Nothing has moved for two seconds: the pointer goes.
    property bool still: false
    /// What the window was before it went full screen, so leaving it gives back a maximised
    /// window maximised.
    property int before: Window.Windowed
    /// The last refusal put in a bubble, until a write lands — see the `Reader` connection
    /// below.
    property string lastRefusal: ""
    /// The part of a wheel turn not yet worth a page: a touchpad sends a notch in slivers.
    property real wheeled: 0
    /// The same, sideways: two fingers across a whole page turn it.
    property real swiped: 0
    /// The same again with Ctrl held, not yet worth a step of zoom.
    property real zoomWheeled: 0
    /// The screen last put on screen, to tell a step back from any other arrival.
    property string shownEntry: ""
    property int shownDisplay: -1
    /// The page landed on from the one after, and not moved since: kept at its foot.
    property bool fromTheFoot: false
    /// The sense of the turn being made, 1 forward and −1 back, for as long as `Reader` takes
    /// to move: what tells a turn, which slides, from a jump, which fades (`arrived`).
    property int turning: 0
    /// The pages last put on screen, and how many screens the book had then.
    property int shownFirst: -1
    property int shownSecond: -1
    property int shownCount: -1
    /// An arrival waiting for its images: "slide", "fade", "cut" — put up in one frame, for
    /// « Sans effet » — or nothing.
    property string pending: ""
    /// Where the screen that slides in starts, in pixels along `pendingAxis` ("x" or "y").
    property real pendingFrom: 0
    property string pendingAxis: "x"
    /// What is behind the pages, as chosen on the settings card.
    readonly property color backdrop: Preferences.backdrop === Preferences.White
                                      ? Theme.readerWhite
                                      : Preferences.backdrop === Preferences.Grey
                                        ? Theme.readerGrey : Theme.readerBlack
    /// Whether anything moves: false for « Sans effet », where a page, the bars and the cards
    /// come and go in one frame.
    readonly property bool animated: Preferences.motion !== Preferences.Instant

    /// The images of the screen on screen are all there.
    readonly property bool pagesReady: {
        const count = pages.count
        for (let i = 0; i < count; ++i) {
            const one = pages.itemAt(i)
            if (!one || !one.loaded)
                return false
        }
        return count > 0
    }
    onPagesReadyChanged: {
        if (view.pagesReady)
            view.startArrival()
    }

    /// How far the screen is enlarged: 1 is the whole page, 4 is 400 %. The keys and the wheel
    /// go by `steps`; a pinch stops anywhere between the first and the last.
    property real zoom: 1
    readonly property var steps: [1, 1.5, 2, 3, 4]
    /// The zoom can be changed: a book read page by page, open, with nothing up over it. Under
    /// the end card, the settings card or the volume's panel, what is up is what the reader is
    /// looking at, and a zoom key changing the page behind it would be a change nobody sees
    /// until the card goes. A webtoon is never zoomed: §09 speaks of a page, and a strip is
    /// already as wide as the window.
    readonly property bool zoomable: !Reader.webtoon && !Reader.loading
                                     && Reader.trouble.length === 0 && !Reader.atTheEnd
                                     && !view.settingsOpen && !view.volumeOpen
                                     && !view.shortcutsOpen
    /// Something of the screen is off it — the zoom, or a tall page fitted to the width — so
    /// the keys move it before they turn it.
    readonly property bool moving: !Reader.webtoon && !Reader.atTheEnd
                                   && (sheet.contentWidth > sheet.width + 0.5
                                       || sheet.contentHeight > sheet.height + 0.5)
    /// What is on screen. A change of it is a page turned, by whatever turned it — a key, a
    /// click, the progress bar, a chapter in the panel — and the page that comes starts
    /// whole, from its top (spec §09).
    readonly property string onScreen: Reader.entryId + "/" + Reader.firstPage + "/"
                                       + Reader.secondPage

    /// The space round the pages in the stage, which is also what the panel keeps from the
    /// window's edges.
    readonly property int margin: Math.round(Math.min(view.width, view.height) * 0.022)

    readonly property bool rightToLeft: Reader.direction === Reader.RightToLeft
    readonly property bool vertical: Reader.direction === Reader.Vertical
    readonly property bool fullScreen: Window.window !== null
                                       && Window.window.visibility === Window.FullScreen
    /// The width each image is asked at: a page's room rounded up to `Reader`'s step and never
    /// shrunk while the volume is open, or the file itself when the fit is its own pixels.
    /// Handed to the pages and to what is fetched ahead alike, so the two ask for the same
    /// file and the second finds it already decoded.
    readonly property int requestWidth: Preferences.fit === Preferences.Original
                                        ? 0 : Reader.askedWidth

    /// The bars go with a turn — and so do the bars a click in the middle was still about to
    /// call: left waiting, they came a moment after the turn, over the page just turned to.
    function sendTheBarsAway() {
        barsLater.stop()
        welcome.stop()
        view.barsShown = false
    }

    /// A page turned by the hand, forward (1) or back (−1): told apart from a jump so that it
    /// slides. `Reader` moves at once, so the sense is only held for the call.
    function turn(sense) {
        view.turning = sense
        if (sense > 0)
            Reader.forward()
        else
            Reader.backward()
        view.turning = 0
    }

    function forward() {
        view.sendTheBarsAway()
        // A webtoon is scrolled, not turned: there is no next page to show, only more strip.
        // At its bottom there is no more strip either, and a forward is what it is in a book:
        // the card, then what follows. Told the last page first, because a volume short
        // enough to fit the window is at its bottom from the start, with nothing scrolled to
        // have said so, and `Reader` would have turned to a page that is not the last.
        if (Reader.webtoon && !Reader.atTheEnd && !strip.atYEnd)
            strip.scrollScreen(1)
        else if (Reader.webtoon && !Reader.atTheEnd) {
            Reader.scrolledTo(Reader.pageCount - 1)
            Reader.forward()
        } else
            view.turn(1)
    }

    function backward() {
        view.sendTheBarsAway()
        // With the card up, a turn back puts it away, in a webtoon as in a book: `Reader` does
        // it, and the strip stays at the bottom it is at.
        if (Reader.atTheEnd)
            Reader.backward()
        else if (Reader.webtoon)
            strip.scrollScreen(-1)
        else
            view.turn(-1)
    }

    /// A side of the screen, or the arrow pointing at it, turned into a sense of reading.
    function turnTowards(left) {
        // A strip has no sides to turn by.
        if (Reader.webtoon)
            return
        if (left === view.rightToLeft)
            view.forward()
        else
            view.backward()
    }

    /// The whole page again, at its top, without a word: what a turned page starts as.
    function whole() {
        glide.stop()
        view.fromTheFoot = false
        view.zoom = 1
        sheet.contentX = 0
        sheet.contentY = 0
    }

    /// A page is put on screen: whole, from its top — or from its foot when the reader went
    /// back one screen, as reading back up a tall page fitted to the width does. Landing on
    /// the top of the page before, the lines just above the ones left behind were a screen
    /// away, and the reader scrolled down again to find their place.
    function arrived() {
        const sameBook = Reader.entryId === view.shownEntry && view.shownEntry.length > 0
        const back = sameBook && Reader.display === view.shownDisplay - 1
        // What the screen left was, read before `whole` puts it back at its top.
        const calm = view.zoom === 1 && !sheet.dragging && !sheet.flicking && !pinch.active
        const sameLayout = sameBook && Reader.displayCount === view.shownCount
        const leaving = [view.shownFirst, view.shownSecond, sheet.contentX, sheet.contentY]
        const sense = view.turning
        view.turning = 0
        view.shownEntry = Reader.entryId
        view.shownDisplay = Reader.display
        view.shownFirst = Reader.firstPage
        view.shownSecond = Reader.secondPage
        view.shownCount = Reader.displayCount
        view.depart(sameBook, sameLayout && calm, sense, leaving)
        // The rest belongs to the page the wheel scrolled: a notch on the page that comes,
        // however it came — by a key, a click — is a notch of its own.
        wheelRest.stop()
        view.wheeled = 0
        // Turned from elsewhere — the progress bar, a chapter in the panel — is turned too.
        barsLater.stop()
        view.whole()
        if (back) {
            view.fromTheFoot = true
            view.toTheFoot()
        }
    }

    /// The screen that was there goes, and the one that came is brought in: see the header.
    /// `leaving` is the screen left — its two pages and where it was scrolled to.
    function depart(sameBook, moving, sense, leaving) {
        // A turn that comes while one is running ends it first: what is on screen is then the
        // screen that was coming, and it is the one that leaves now. One still waiting for its
        // images has put nothing new on screen: the screen leaving is still the ghost's.
        const waiting = view.pending.length > 0
        turnSlide.complete()
        turnFade.complete()
        view.settle()
        if (Reader.webtoon || Reader.firstPage < 0 || (sameBook && !moving))
            return
        // Another book's pages are not this one's: a volume opened fades in over the paper.
        if (!sameBook) {
            ghost.first = -1
            ghost.second = -1
        } else if (!waiting) {
            ghost.first = leaving[0]
            ghost.second = leaving[1]
            ghost.scrolledX = leaving[2]
            ghost.scrolledY = leaving[3]
        }
        ghost.visible = sameBook && ghost.first >= 0
        if (!view.animated) {
            // Hidden as a fade hides it, and shown whole once its images are there.
            view.pending = "cut"
            sheet.opacity = 0
        } else if (sameBook && sense !== 0 && Preferences.motion === Preferences.Slide) {
            view.pending = "slide"
            view.pendingAxis = view.vertical ? "y" : "x"
            const span = view.vertical ? stage.height : stage.width
            // Going on in a book read left to right, or back in a manga, the screen that comes
            // lies to the right; going on in a book read downwards, below.
            const fromAfter = view.vertical ? sense > 0 : (sense > 0) !== view.rightToLeft
            view.pendingFrom = fromAfter ? span : -span
            // Out of sight until its images are there; the ghost stands meanwhile.
            sheetShift[view.pendingAxis] = view.pendingFrom
        } else {
            view.pending = "fade"
            sheet.opacity = 0
        }
        arrivalHeld.restart()
        Qt.callLater(view.startArrival)
    }

    /// The arrival waiting starts, once its images are there or once it has waited long
    /// enough: a page the server is slow to give is not worth a frozen screen.
    function startArrival(anyway) {
        if (view.pending.length === 0 || (!view.pagesReady && anyway !== true))
            return
        arrivalHeld.stop()
        if (view.pending === "cut") {
            view.settle()
        } else if (view.pending === "slide") {
            turnIn.property = view.pendingAxis
            turnIn.from = view.pendingFrom
            turnIn.to = 0
            turnOut.property = view.pendingAxis
            turnOut.from = 0
            turnOut.to = -view.pendingFrom
            view.pending = ""
            turnSlide.start()
        } else {
            view.pending = ""
            turnFade.start()
        }
    }

    /// Everything a turn moved, put back: the screen in its place, the ghost gone.
    function settle() {
        arrivalHeld.stop()
        view.pending = ""
        sheetShift.x = 0
        sheetShift.y = 0
        ghostShift.x = 0
        ghostShift.y = 0
        sheet.opacity = 1
        ghost.opacity = 1
        ghost.visible = false
    }

    /// At the foot, for as long as the reader has not moved since landing there: the page's
    /// height is laid out a frame later, and again when its image arrives with its own shape.
    function toTheFoot() {
        if (view.fromTheFoot)
            sheet.contentY = Math.max(0, sheet.contentHeight - sheet.height)
    }

    /// Ctrl 0: the whole page, and the level said, since the reader asked for it.
    function backToWhole() {
        if (view.zoom === 1)
            return
        view.whole()
        levelHeld.restart()
    }

    /// The screen at `level`, with the point at (`x`, `y`) of the sheet staying where it is:
    /// the pointer, the fingers, or the middle of the screen for a key. A zoom about a fixed
    /// corner sends what was aimed at off the screen, and the reader goes looking for it.
    function zoomTo(level, x, y) {
        const to = Math.max(view.steps[0], Math.min(view.steps[view.steps.length - 1], level))
        if (Math.abs(to - view.zoom) < 0.001)
            return
        glide.stop()
        view.fromTheFoot = false
        // The row places its pages at the next frame, and measured before that it still had
        // the height of the window before a resize: the point aimed at came out four pixels
        // off. Placed now, it is measured as drawn.
        spread.forceLayout()
        const from = view.zoom
        // The point aimed at, in the spread's own unzoomed pixels.
        const u = (sheet.contentX + x - spread.x) / from
        const v = (sheet.contentY + y - spread.y) / from
        // The canvas and the spread follow at once: their sizes are bindings on the zoom, and
        // a scale changes neither the spread's width nor its height.
        view.zoom = to
        sheet.contentX = Math.max(0, Math.min(canvas.width - sheet.width, spread.x + u * to - x))
        sheet.contentY = Math.max(0, Math.min(canvas.height - sheet.height,
                                              spread.y + v * to - y))
        levelHeld.restart()
    }

    /// A step of `steps` up (1) or down (−1), about the middle of the screen unless a point
    /// is given.
    function stepZoom(direction, x, y) {
        let level = view.zoom
        if (direction > 0)
            level = view.steps.find(step => step > view.zoom + 0.001) ?? view.zoom
        else
            level = view.steps.slice().reverse().find(step => step < view.zoom - 0.001)
                    ?? view.zoom
        view.zoomTo(level, x === undefined ? sheet.width / 2 : x,
                    y === undefined ? sheet.height / 2 : y)
    }

    /// Where the page is going: the end of the glide under way, else where it is. A key
    /// pressed during a glide aims from there, so two quick presses go twice as far and the
    /// edge is known before the page has reached it.
    function aimX() {
        return glide.running ? glideX.to : sheet.contentX
    }

    function aimY() {
        return glide.running ? glideY.to : sheet.contentY
    }

    /// Moves what is off the screen by (`dx`, `dy`), gliding, as far as its edge; false when
    /// the edge was already there, which is when the key turns the page instead.
    function nudge(dx, dy) {
        const fromX = view.aimX()
        const fromY = view.aimY()
        const x = Math.max(0, Math.min(sheet.contentWidth - sheet.width, fromX + dx))
        const y = Math.max(0, Math.min(sheet.contentHeight - sheet.height, fromY + dy))
        if (Math.abs(x - fromX) < 0.5 && Math.abs(y - fromY) < 0.5)
            return false
        view.fromTheFoot = false
        glide.stop()
        glideX.from = sheet.contentX
        glideX.to = x
        glideY.from = sheet.contentY
        glideY.to = y
        glide.start()
        return true
    }

    /// The same at once, for the wheel and the touchpad, which send a stream: a glide per
    /// sliver would always be catching up with the fingers.
    function scrollPage(dx, dy) {
        glide.stop()
        view.fromTheFoot = false
        const x = Math.max(0, Math.min(sheet.contentWidth - sheet.width, sheet.contentX + dx))
        const y = Math.max(0, Math.min(sheet.contentHeight - sheet.height, sheet.contentY + dy))
        const moved = Math.abs(x - sheet.contentX) >= 0.5 || Math.abs(y - sheet.contentY) >= 0.5
        sheet.contentX = x
        sheet.contentY = y
        return moved
    }

    /// The wheel, from wherever it was turned, at (`x`, `y`) of the view.
    function wheel(event, x, y) {
        // Over the settings card or the volume's panel the wheel is theirs. The panel's list
        // scrolls itself, but the card does not scroll, and three notches over it turned three
        // pages behind it.
        if (view.over(card, x, y) || (view.volumeOpen && view.over(panel, x, y)))
            return
        // Under the shortcuts card the wheel is nobody's: the card scrolls itself when it is
        // taller than the window, and the page behind it is not being read.
        if (view.shortcutsOpen)
            return
        if (Reader.webtoon) {
            // Ctrl or not: the strip is not zoomed, and a Ctrl left held must not stop it
            // scrolling.
            const down = -(event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y)
            if (!Reader.atTheEnd) {
                const from = strip.contentY
                strip.scrollBy(down)
                if (Math.abs(strip.contentY - from) >= 0.5) {
                    view.wheeled = 0
                    // Reaching the foot is not also going on: the rest a tall page fitted to
                    // the width has, for the same reason (below).
                    if (strip.atYEnd)
                        wheelRest.restart()
                    return
                }
                // Nothing moved. At the top that is all; at the foot the wheel goes on as the
                // space bar does — the card, then what follows — which a strip with nothing
                // left to scroll never did, and the card was out of the wheel's reach.
                if (down <= 0 || !strip.atYEnd)
                    return
                if (wheelRest.running || event.phase === Qt.ScrollMomentum) {
                    view.wheeled = 0
                    return
                }
            }
            view.notch(event)
            return
        }
        if (event.modifiers & Qt.ControlModifier) {
            if (!view.zoomable)
                return
            const at = sheet.mapFromItem(view, x, y)
            view.zoomWheeled += event.angleDelta.y
            while (view.zoomWheeled >= 120) {
                view.zoomWheeled -= 120
                view.stepZoom(1, at.x, at.y)
            }
            while (view.zoomWheeled <= -120) {
                view.zoomWheeled += 120
                view.stepZoom(-1, at.x, at.y)
            }
            return
        }
        const dx = -(event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.angleDelta.x)
        const dy = -(event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y)
        // Zoomed, the wheel scrolls and never turns: a page enlarged was enlarged to be
        // looked at, and a flick of the wheel past its foot would throw it away (spec §09).
        if (view.zoom > 1 && view.moving) {
            view.scrollPage(dx, dy)
            return
        }
        // A tall page fitted to the width scrolls down first, and the wheel turns it at the
        // foot, as it turns a page that fits — but not with the scroll that brought it
        // there. A touchpad goes on sending the motion its fingers had once they are lifted,
        // and that run reaching the foot turned the page under a reader who had only meant
        // to read down to it. Swallowed while the scroll is fresh (`wheelRest`) and whenever
        // the platform names it momentum; the next deliberate notch turns. `phase` is on the
        // QML wheel event since Qt 6.0, without a revision (qquickevents_p_p.h, 6.4.2), so the
        // Qt 6.4 the CI builds with reads it too.
        if (view.moving) {
            if (view.scrollPage(dx, dy)) {
                view.wheeled = 0
                wheelRest.restart()
                return
            }
            if (wheelRest.running || event.phase === Qt.ScrollMomentum) {
                view.wheeled = 0
                return
            }
        }
        // Two fingers sideways across a whole page turn it, as the arrow on that side does.
        // Once per gesture: the fingers lifted, a touchpad keeps sending the motion it had,
        // and that run of slivers would turn a page each 120.
        if (Math.abs(event.angleDelta.x) > Math.abs(event.angleDelta.y)) {
            if (swipeRest.running) {
                swipeRest.restart()
                return
            }
            view.swiped += event.angleDelta.x
            if (Math.abs(view.swiped) >= 120) {
                view.turnTowards(view.swiped > 0)
                view.swiped = 0
                swipeRest.restart()
            }
            return
        }
        view.notch(event)
    }

    /// A page a notch, which is 120 in Qt's eighths of a degree, down going on. A turn of the
    /// wheel's own on a page that has not finished arriving would otherwise skip it.
    ///
    /// With the end card up, the next notch going on opens what follows (spec §07) — but not
    /// the notch that raised it, nor the hand still rolling: one notch raised the card and
    /// the next opened the next volume under a reader who had not seen it, and a touchpad's
    /// momentum did it alone. The rest starts when the card rises (`onAtTheEndChanged`), and
    /// momentum is never a decision.
    function notch(event) {
        if (Reader.atTheEnd && (wheelRest.running || event.phase === Qt.ScrollMomentum)) {
            view.wheeled = 0
            return
        }
        view.wheeled += event.angleDelta.y
        while (view.wheeled <= -120) {
            view.wheeled += 120
            view.forward()
            // A turn of two notches at once: the first raised the card, the second is not
            // a decision about it.
            if (Reader.atTheEnd && wheelRest.running) {
                view.wheeled = 0
                return
            }
        }
        while (view.wheeled >= 120) {
            view.wheeled -= 120
            view.backward()
        }
    }

    /// Whether (`x`, `y`) of the view is on `item`, drawn.
    function over(item, x, y) {
        return item.visible && x >= item.x && x < item.x + item.width && y >= item.y
               && y < item.y + item.height
    }

    /// A tap, from wherever it landed, at (`x`, `y`) of the view, and the how-manieth in a
    /// row. The second of a double click zooms where it points, or gives the whole page back
    /// — but only when the first is still waiting to call the bars, which is in the middle
    /// third of a whole page, or anywhere on a zoomed one. A double click on a third of a
    /// whole page is two turns, deliberately: telling it from a single click means holding
    /// every click back for the double-click interval, and that delay would fall on every
    /// page of every book turned by a click, for a zoom wanted now and then.
    function tapped(x, y, count) {
        if (count === 2 && barsLater.running) {
            barsLater.stop()
            if (view.zoom > 1) {
                view.backToWhole()
                return
            }
            const at = sheet.mapFromItem(view, x, y)
            view.zoomTo(2, at.x, at.y)
            return
        }
        view.tappedAt(x, y)
    }

    function toggleFullScreen() {
        const window = Window.window
        if (!window)
            return
        if (view.fullScreen) {
            view.leaveFullScreen()
            return
        }
        view.before = window.visibility
        window.showFullScreen()
    }

    function leaveFullScreen() {
        const window = Window.window
        if (!window || !view.fullScreen)
            return
        if (view.before === Window.Maximized)
            window.showMaximized()
        else
            window.showNormal()
    }

    /// What Escape does here, one thing at a time, and true once it has done one. The end
    /// card comes first and leaves the reader on the last page; then, each closed before
    /// anything behind it, the shortcuts card, the settings card and the volume's panel,
    /// which are never open together; then the full screen, which a reader may have entered to read and not to
    /// leave; and only then the reader itself.
    function peelOneLayer() {
        if (Reader.atTheEnd) {
            // Going back from the end is how `Reader` puts the card away, whatever it was
            // that raised it.
            Reader.backward()
            return true
        }
        if (view.shortcutsOpen) {
            view.shortcutsOpen = false
            return true
        }
        if (view.settingsOpen) {
            view.settingsOpen = false
            return true
        }
        if (view.volumeOpen) {
            view.volumeOpen = false
            return true
        }
        if (view.fullScreen) {
            view.leaveFullScreen()
            return true
        }
        Navigation.back()
        return true
    }

    /// A click on the page: a third turns, the middle calls or sends away the bars. A click
    /// on a bar is the bar's, and is not a click on the page under it.
    function tappedAt(x, y) {
        // The end card is the card's, as the settings card is: its buttons act on their own,
        // and a tap on its face is not a turn. Elsewhere a click does what it does on any
        // page: the third going on does what the card's button does (spec §07), the other
        // third puts the card away, and the middle calls the bars.
        if (Reader.atTheEnd && endCard.holds(x, y))
            return
        const onBar = y < bar.height
        const onDock = y >= dock.y && y < dock.y + dock.height
                       && x >= dock.x && x < dock.x + dock.width
        if (view.shortcutsOpen) {
            // The card is the card's, and anywhere else is a click away from it, which closes
            // it and turns nothing: the veil is over the page, and the page is not being read.
            // The bars' own buttons stay theirs.
            const face = keys.card.mapToItem(view, 0, 0)
            if (x >= face.x && x < face.x + keys.card.width && y >= face.y
                    && y < face.y + keys.card.height)
                return
            if (!(view.barsShown && (onBar || onDock)))
                view.shortcutsOpen = false
            return
        }
        if (view.settingsOpen) {
            // The card is the card's. Anywhere else on the page is a click away from it, which
            // closes it and is not also a turn: the reader meant to leave the card, and a page
            // that turned under that click would have lost their place.
            if (x >= card.x && x < card.x + card.width && y >= card.y
                    && y < card.y + card.height)
                return
            if (!onBar && !onDock) {
                view.settingsOpen = false
                return
            }
        }
        if (view.volumeOpen) {
            // The same rule as the card's: the panel is the panel's, a bar or the dock is
            // theirs — the book button toggles the panel itself, and a close here as well
            // would reopen it — and anywhere else on the page is a click away, which is not
            // also a turn.
            if (x >= panel.x && x < panel.x + panel.width && y >= panel.y
                    && y < panel.y + panel.height)
                return
            if (!(view.barsShown && (onBar || onDock))) {
                view.volumeOpen = false
                return
            }
        }
        if (view.barsShown && (onBar || onDock))
            return
        // Zoomed, a click turns nothing: the page is moved, not turned (spec §09), and a
        // third of it is only somewhere in a picture.
        if (view.zoom === 1 && x < view.width / 3)
            view.turnTowards(true)
        else if (view.zoom === 1 && x >= view.width * 2 / 3)
            view.turnTowards(false)
        else if (view.zoomable)
            // Held for as long as a double click takes, which may come to zoom instead. Only
            // the bars wait: they are not where the reader is reading.
            barsLater.restart()
        else
            view.toggleTheBars()
    }

    /// A click in the middle: the reader takes the bars in hand, and the bars a volume opened
    /// with no longer leave on their own.
    function toggleTheBars() {
        welcome.stop()
        view.barsShown = !view.barsShown
    }

    /// The pointer or a key moved: the bars a volume opened with wait three seconds more.
    function stirred() {
        view.still = false
        stillness.restart()
        if (welcome.running)
            welcome.restart()
    }

    objectName: "reader-view"
    focus: true

    onOnScreenChanged: view.arrived()

    /// The volume open and drawn, or empty while it opens: a change to an entry is a volume
    /// opened — from the series, or from the end card — and it opens with the bars up.
    readonly property string openedEntry: Reader.loading || Reader.trouble.length > 0
                                          || Reader.pageCount === 0 ? "" : Reader.entryId
    onOpenedEntryChanged: {
        if (view.openedEntry.length === 0)
            return
        barsLater.stop()
        view.barsShown = true
        welcome.restart()
    }

    onBarsShownChanged: {
        if (!view.barsShown)
            view.settingsOpen = false
    }
    // One or the other: both are things hung from the bar, and a card over a panel over the
    // pages is a screen with nothing left to read.
    onSettingsOpenChanged: {
        if (view.settingsOpen) {
            view.volumeOpen = false
            view.shortcutsOpen = false
            // The card hangs from the bar: the bars stay for as long as it is open.
            welcome.stop()
        }
    }
    /// The card at the end is up; see `notch`.
    readonly property bool ending: Reader.atTheEnd
    onEndingChanged: {
        if (view.ending)
            wheelRest.restart()
    }

    // The card at the end is the only thing on the screen then: a panel left open beside it
    // would be two cards for one decision.
    Connections {
        target: Reader

        function onChanged() {
            if (Reader.atTheEnd) {
                view.settingsOpen = false
                view.volumeOpen = false
                view.shortcutsOpen = false
            }
        }
    }
    onShortcutsOpenChanged: {
        if (view.shortcutsOpen) {
            view.settingsOpen = false
            view.volumeOpen = false
        }
    }

    /// The shortcuts card, opened or put away — by its button, `?` or F1. Nothing opens over
    /// the end card, which is the one decision on the screen.
    function toggleShortcuts() {
        if (view.shortcutsOpen)
            view.shortcutsOpen = false
        else if (!Reader.atTheEnd)
            view.shortcutsOpen = true
    }

    onVolumeOpenChanged: {
        if (view.volumeOpen) {
            view.settingsOpen = false
            view.shortcutsOpen = false
            welcome.stop()
        }
    }

    // The window the pages are laid out in. Main tells `Reader` before it opens anything —
    // the list of pages is asked at one page's width — and this follows every change after.
    // The pages keep the whole window whatever is open over them: the volume's panel is a
    // card laid over the right of the page, not a column taken from it (see `panel`).
    onWidthChanged: relayout()
    onHeightChanged: relayout()
    function relayout() {
        if (view.width > 0 && view.height > 0)
            Reader.relayout(Qt.size(view.width, view.height))
    }

    Component.onCompleted: view.forceActiveFocus()
    // A full screen entered to read is left with the book: the series page under it was
    // never asked to fill the screen.
    Component.onDestruction: view.leaveFullScreen()

    /// The first or the last page, by the side it lies on: in a book read left to right the
    /// first is on the left, in a manga the last is — so Ctrl ← goes where ← goes on. A jump,
    /// and from the end card a jump too: the card is put away first.
    function toAnEnd(left) {
        const last = left === view.rightToLeft
        if (Reader.atTheEnd)
            Reader.backward()
        if (Reader.webtoon)
            strip.toTheEnd(last)
        else
            Reader.goToPage(last ? Reader.pageCount - 1 : 0)
    }

    // The keys the test of 22/10/2026 reached for and found doing nothing: F11 for the full
    // screen, Backspace to go back, Alt ← and Ctrl W to leave, + and − without Ctrl, Ctrl ←
    // and Ctrl → for the ends of the book, Enter on the end card.
    Keys.onPressed: event => {
        view.stirred()
        // Alt ← is the way back of every browser and file manager: the series, at once, and
        // not one layer at a time as Escape goes.
        if (event.modifiers & Qt.AltModifier) {
            if (event.key === Qt.Key_Left) {
                Navigation.back()
                event.accepted = true
            }
            return
        }
        if (event.modifiers & Qt.ControlModifier) {
            switch (event.key) {
            case Qt.Key_W:
                Navigation.back()
                break
            case Qt.Key_Left:
                if (!view.shortcutsOpen)
                    view.toAnEnd(true)
                break
            case Qt.Key_Right:
                if (!view.shortcutsOpen)
                    view.toAnEnd(false)
                break
            // Ctrl and the keypad's + and −, which the zoom shortcuts below do not match on
            // every keyboard: the keypad is a modifier of its own to them.
            case Qt.Key_Plus:
                if (view.zoomable)
                    view.stepZoom(1)
                break
            case Qt.Key_Minus:
                if (view.zoomable)
                    view.stepZoom(-1)
                break
            default:
                return
            }
            event.accepted = true
            return
        }
        if (event.modifiers & Qt.MetaModifier)
            return
        // `?` — Shift and a comma on a French keyboard — and F1 open and close the card of the
        // keys; with the card up, the other keys wait for it to be put away.
        if (event.key === Qt.Key_Question || event.key === Qt.Key_F1) {
            view.toggleShortcuts()
            event.accepted = true
            return
        }
        if (view.shortcutsOpen)
            return
        // What is off the screen is moved by a third of it at an arrow, and by most of it at
        // the space bar, so the last lines read stay in view; at the edge, the key turns.
        const along = Math.round(sheet.width / 3)
        const down = Math.round(sheet.height / 3)
        const screen = Math.round(sheet.height * 0.9)
        switch (event.key) {
        case Qt.Key_Left:
            if (Reader.webtoon)
                return
            if (!(view.moving && view.nudge(-along, 0)))
                view.turnTowards(true)
            break
        case Qt.Key_Right:
            if (Reader.webtoon)
                return
            if (!(view.moving && view.nudge(along, 0)))
                view.turnTowards(false)
            break
        case Qt.Key_Down:
            if (view.moving && view.nudge(0, down))
                break
            // Past the foot of a page that was being moved, down goes on, as the space bar
            // does; a whole page is turned by `↓` only in a book read downwards.
            if (!view.vertical && !Reader.webtoon && !view.moving)
                return
            view.forward()
            break
        case Qt.Key_Up:
            if (view.moving && view.nudge(0, -down))
                break
            if (!view.vertical && !Reader.webtoon && !view.moving)
                return
            view.backward()
            break
        case Qt.Key_Space:
            // Shift+Space is the way back, as in every scrolled document.
            if (event.modifiers & Qt.ShiftModifier) {
                if (!(view.moving && view.nudge(0, -screen)))
                    view.backward()
            } else if (!(view.moving && view.nudge(0, screen))) {
                view.forward()
            }
            break
        case Qt.Key_PageDown:
            if (!(view.moving && view.nudge(0, screen)))
                view.forward()
            break
        case Qt.Key_Home:
            if (!Reader.webtoon)
                return
            strip.toTheEnd(false)
            break
        case Qt.Key_End:
            if (!Reader.webtoon)
                return
            strip.toTheEnd(true)
            break
        case Qt.Key_PageUp:
            if (!(view.moving && view.nudge(0, -screen)))
                view.backward()
            break
        case Qt.Key_F:
        case Qt.Key_F11:
            view.toggleFullScreen()
            break
        // Back a page, as the dock's button: from the first page, the volume before.
        case Qt.Key_Backspace:
            view.backward()
            break
        // + and − alone, as with Ctrl: a hand on the mouse has the other free for one key.
        case Qt.Key_Plus:
            if (view.zoomable)
                view.stepZoom(1)
            break
        case Qt.Key_Minus:
            if (view.zoomable)
                view.stepZoom(-1)
            break
        // Enter on the end card is its button. Anywhere else it is nothing here.
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (!Reader.atTheEnd)
                return
            view.forward()
            break
        case Qt.Key_I:
            // Nothing opens over the end card, which is the one decision on the screen.
            if (!Reader.atTheEnd)
                view.volumeOpen = !view.volumeOpen
            break
        default:
            return
        }
        event.accepted = true
    }

    Rectangle {
        objectName: "reader-backdrop"
        anchors.fill: parent
        color: view.backdrop
    }

    // The pages, centred, alone or in a pair. In a pair the page read first is on the side
    // reading starts from — the right in a manga — so the row is mirrored rather than its
    // pages swapped, and a pair always meets at the spine.
    Item {
        id: stage

        objectName: "reader-stage"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: view.margin
        width: view.width - 2 * view.margin
        clip: true
        visible: !Reader.webtoon && !Reader.loading && Reader.trouble.length === 0

        readonly property bool paired: Reader.secondPage >= 0

        // The screen being left, drawn while it goes: under the sheet, at the place it had —
        // centred as the canvas centres it, scrolled as far as it was. Its pages are those
        // that were on screen, asked at the same width, so the image cache hands their files
        // back as they were decoded: nothing is fetched or decoded again for it.
        Item {
            id: ghost

            /// The pages it draws, −1 for none.
            property int first: -1
            property int second: -1
            /// Where the sheet had been scrolled to when it was left.
            property real scrolledX: 0
            property real scrolledY: 0

            objectName: "reader-ghost"
            width: stage.width
            height: stage.height
            visible: false

            transform: Translate {
                id: ghostShift
            }

            Row {
                id: ghostSpread

                x: Math.max(0, (ghost.width - ghostSpread.width) / 2) - ghost.scrolledX
                y: Math.max(0, (ghost.height - ghostSpread.height) / 2) - ghost.scrolledY
                layoutDirection: view.rightToLeft ? Qt.RightToLeft : Qt.LeftToRight

                Repeater {
                    model: ghost.first < 0 ? 0 : ghost.second >= 0 ? 2 : 1

                    ReaderPage {
                        id: left

                        required property int index

                        name: "reader-ghost"
                        page: left.index === 0 ? ghost.first : ghost.second
                        y: (ghostSpread.height - height) / 2
                        slotWidth: ghost.second >= 0 ? stage.width / 2 : stage.width
                        slotHeight: stage.height
                        requestWidth: view.requestWidth
                    }
                }
            }
        }

        // What is off the screen, moved. Dragged by hand only when there is something off
        // it; the keys and the wheel move it from `ReaderView` and never through the
        // Flickable's own wheel, which would scroll a page that Ctrl and the wheel meant to
        // zoom.
        Flickable {
            id: sheet

            objectName: "reader-sheet"
            anchors.fill: parent
            contentWidth: canvas.width
            contentHeight: canvas.height
            interactive: view.moving
            boundsBehavior: Flickable.StopAtBounds

            onContentHeightChanged: view.toTheFoot()
            onMovementStarted: view.fromTheFoot = false

            // Where a turn slides it from; nought at rest.
            transform: Translate {
                id: sheetShift

                objectName: "reader-sheet-shift"
            }

            // As wide and as tall as the screen at least, so a page smaller than it stays
            // centred; the spread laid out whole inside, and scaled. Scaled and not laid out
            // again larger: the pages keep the size their fit gives them, so the zoom never
            // asks `Reader` for another layout, and the file drawn over a page once it is
            // zoomed past its copy (`ReaderPage`) is sampled at the scale, not stretched.
            Item {
                id: canvas

                width: Math.max(sheet.width, spread.width * view.zoom)
                height: Math.max(sheet.height, spread.height * view.zoom)

                // In a pair the page read first is on the side reading starts from.
                Row {
                    id: spread

                    x: (canvas.width - spread.width * view.zoom) / 2
                    y: (canvas.height - spread.height * view.zoom) / 2
                    scale: view.zoom
                    transformOrigin: Item.TopLeft
                    layoutDirection: view.rightToLeft ? Qt.RightToLeft : Qt.LeftToRight

                    // A count and not the list of pages: a list is a new model at every
                    // `Reader.changed` — a window resized, a write landed — and the Repeater
                    // made every page again, blank until its image was decoded anew. A count
                    // changes only between one page and a pair; each page follows what it
                    // shows.
                    Repeater {
                        id: pages

                        model: Reader.webtoon || Reader.firstPage < 0 ? 0 : stage.paired ? 2 : 1

                        ReaderPage {
                            id: shown

                            required property int index

                            page: shown.index === 0 ? Reader.firstPage : Reader.secondPage
                            // A pair of pages of two heights meets at the middle. A page
                            // taller than the screen, at the width fit, starts at its top:
                            // the canvas holds it from there, and the keys move it down.
                            y: (spread.height - height) / 2
                            slotWidth: stage.paired ? stage.width / 2 : stage.width
                            slotHeight: stage.height
                            requestWidth: view.requestWidth
                            zoom: view.zoom
                        }
                    }
                }
            }

            // Here and not only on the reader: a Flickable keeps the press for itself in case
            // it becomes a drag, and a handler on its parent saw no click on a page that could
            // be dragged — the strip learned it first.
            TapHandler {
                id: pageTap

                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: {
                    const at = view.mapFromItem(null, pageTap.point.scenePosition)
                    view.tapped(at.x, at.y, pageTap.tapCount)
                }
            }

            WheelHandler {
                id: pageWheel

                target: null
                onWheel: event => {
                    const at = view.mapFromItem(null, pageWheel.point.scenePosition)
                    view.wheel(event, at.x, at.y)
                }
            }

            // A pinch on the touchpad, about the fingers, anywhere between whole and 400 %;
            // pinched back all the way, the page is whole again. Not snapped to `steps`:
            // fingers stop where the reader wants, and the next key goes on from there.
            PinchHandler {
                id: pinch

                property real from: 1

                target: null
                enabled: view.zoomable
                onActiveChanged: {
                    if (pinch.active)
                        pinch.from = view.zoom
                    else if (view.zoom < 1.05)
                        view.backToWhole()
                }
                // Through the scene, as the tap and the wheel are: the handler's own
                // coordinates are not promised to be the sheet's viewport.
                onActiveScaleChanged: {
                    const at = sheet.mapFromItem(null, pinch.centroid.scenePosition)
                    view.zoomTo(pinch.from * pinch.activeScale, at.x, at.y)
                }
            }

            ParallelAnimation {
                id: glide
                // Named so the tests can wait for it to stop: a key lands by animating, and
                // a test that reads a position or presses again mid-glide reads a page in
                // motion.
                objectName: "reader-sheet-glide"

                NumberAnimation {
                    id: glideX

                    target: sheet
                    property: "contentX"
                    duration: view.animated ? 180 : 0
                    easing.type: Easing.OutCubic
                }

                NumberAnimation {
                    id: glideY

                    target: sheet
                    property: "contentY"
                    duration: view.animated ? 180 : 0
                    easing.type: Easing.OutCubic
                }
            }
        }
    }

    // A turn: the screen that comes in from the side reading goes towards, the one left out
    // the other way, at once (see the header).
    ParallelAnimation {
        id: turnSlide

        objectName: "reader-turn"

        NumberAnimation {
            id: turnIn

            objectName: "reader-turn-in"
            target: sheetShift
            property: "x"
            duration: 200
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            id: turnOut

            objectName: "reader-turn-out"
            target: ghostShift
            property: "x"
            duration: 200
            easing.type: Easing.OutCubic
        }

        onFinished: view.settle()
    }

    // A jump: the screen that comes over the one left, which fades out under it.
    ParallelAnimation {
        id: turnFade

        objectName: "reader-fade"

        NumberAnimation {
            target: sheet
            property: "opacity"
            from: 0
            to: 1
            duration: 150
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: ghost
            property: "opacity"
            from: 1
            to: 0
            duration: 150
            easing.type: Easing.OutCubic
        }

        onFinished: view.settle()
    }

    // How long an arrival waits for its images before it goes anyway.
    Timer {
        id: arrivalHeld

        interval: 250
        onTriggered: view.startArrival(true)
    }

    // A webtoon: one strip, edge to edge, over the whole window — no margin round it, a
    // strip has no page to leave paper around.
    ReaderStrip {
        id: strip

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: view.width
        requestWidth: view.requestWidth
        onTapped: (x, y) => view.tappedAt(x, y)
    }

    // The pages after these and the one before, fetched while this one is read: turning must
    // never show an empty page (spec §10). Never drawn — an `Image` loads whether or not it
    // is shown — and dropped as soon as the reader moves away from them, which cancels what
    // has not arrived.
    //
    // The properties read before the call are what make it follow: a binding tracks the
    // properties it reads, and a call to an invokable registers none. Written as the call
    // alone, the list was computed once — while the book was still opening, so empty — and
    // never again, and nothing was ever fetched ahead.
    Repeater {
        model: {
            // What `ahead` reads — the screen, the layout, what follows — all notify through
            // these two. Read here so the binding is told; their values are not used, and no
            // condition is made of them: `display` is never below 0 and `next` is a map,
            // never null.
            void Reader.display
            void Reader.next
            // A strip is fetched by its list, a screen ahead: nothing is held beside it.
            return Reader.webtoon ? [] : Reader.ahead(view.requestWidth)
        }

        Image {
            id: early

            required property string modelData

            source: early.modelData
            asynchronous: true
            // The page's own fill, and not for the look — this is never drawn. The image cache
            // files a decoded image under its address *and* the options it was asked with,
            // the fill among them, so a page fetched ahead with the default fill was a second
            // entry beside the page drawn with `PreserveAspectFit`: every page turned to was
            // asked of the server again and decoded again. Found on 22/10/2026 counting the
            // requests one turn made; the same options make the turn find what was fetched.
            // The next volume's cover is drawn cropped by the end card, and fetched so.
            fillMode: early.modelData.endsWith("/cover") ? Image.PreserveAspectCrop
                                                         : Image.PreserveAspectFit
            visible: false
        }
    }

    // What went wrong instead of the book: `Reader` words it, this only lays it out.
    Text {
        objectName: "reader-trouble"
        anchors.centerIn: parent
        // Follows the window as the bar and the cards do (owner's test of 22/10/2026).
        width: Math.min(parent.width - 64, Widths.scale.column(520))
        visible: Reader.trouble.length > 0
        text: Reader.trouble
        // What the server answered may be in it, and that is text, not markup.
        textFormat: Text.PlainText
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.body(15)
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
    }

    // The paper round the pages; the pages take their own clicks and wheel (see `sheet`), and
    // the strip its clicks.
    TapHandler {
        id: paperTap

        enabled: !Reader.webtoon
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: view.tapped(paperTap.point.position.x, paperTap.point.position.y,
                              paperTap.tapCount)
    }

    WheelHandler {
        id: paperWheel

        target: null
        onWheel: event => view.wheel(event, paperWheel.point.position.x,
                                     paperWheel.point.position.y)
    }

    // The keys of §09, as shortcuts so that the focused item cannot swallow them first. Off
    // while something is up over the page, see `zoomable`.
    Shortcut {
        // StandardKey is a global QML enumeration: there is deliberately no object to qualify.
        // qmllint disable unqualified
        // Ctrl = as well: Ctrl + on a keyboard whose + needs Shift is Ctrl Shift =, and a
        // reader presses Ctrl = and expects the same.
        sequences: [StandardKey.ZoomIn, "Ctrl+="]
        // qmllint enable unqualified
        enabled: view.zoomable
        onActivated: view.stepZoom(1)
    }

    Shortcut {
        // qmllint disable unqualified
        sequences: [StandardKey.ZoomOut]
        // qmllint enable unqualified
        enabled: view.zoomable
        onActivated: view.stepZoom(-1)
    }

    Shortcut {
        // On a French keyboard 0 is the key that types `à`, and without Shift Ctrl and that
        // key arrive as Ctrl À, which matches no « Ctrl+0 »: both are written out.
        sequences: ["Ctrl+0", "Ctrl+À"]
        enabled: view.zoomable
        onActivated: view.backToWhole()
    }

    // A click in the middle calls the bars once it is sure not to be the first of a double
    // click; see `tapped`.
    Timer {
        id: barsLater

        objectName: "reader-bars-later"
        // The desktop's own double click, through `Qt.styleHints`: `Application.styleHints`
        // is typed, and newer than the Qt 6.4 the CI builds with. Fifty milliseconds more:
        // a second click landing right on the interval would otherwise find the bars already
        // called, and be read as a click of its own.
        // qmllint disable missing-property
        interval: Qt.styleHints.mouseDoubleClickInterval + 50
        // qmllint enable missing-property
        onTriggered: view.toggleTheBars()
    }

    // The bars a volume opens with, sent away after three seconds without input; see the
    // header.
    Timer {
        id: welcome

        objectName: "reader-welcome"
        interval: 3000
        onTriggered: view.barsShown = false
    }

    // The level said for a second in the bar at the foot, then nothing (spec §09).
    Timer {
        id: levelHeld

        objectName: "reader-zoom-held"
        interval: 1000
    }

    // The rest after the wheel scrolled a page: reaching the foot is not also a turn.
    Timer {
        id: wheelRest

        objectName: "reader-wheel-rest"
        interval: 300
    }

    // The rest after two fingers turned a page sideways: what the touchpad still sends of
    // that motion is not a second turn.
    Timer {
        id: swipeRest

        interval: 300
        onTriggered: view.swiped = 0
    }

    HoverHandler {
        // Not under the shortcuts card either: its cross is something to aim at.
        cursorShape: view.still && !view.barsShown && !view.shortcutsOpen ? Qt.BlankCursor
                                                                          : Qt.ArrowCursor
        onPointChanged: view.stirred()
    }

    Timer {
        id: stillness

        interval: 2000
        running: true
        onTriggered: view.still = true
    }

    // Over the pages and under the bars: the bars are what a reader calls to leave.
    ReaderNextCard {
        id: endCard

        anchors.fill: parent
        onReadNextRequested: view.forward()
        onStayRequested: Reader.backward()
        onBackToSeriesRequested: Navigation.back()
    }

    ReaderBar {
        id: bar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        opacity: view.barsShown ? 1 : 0
        visible: opacity > 0
        fullScreen: view.fullScreen
        onBackRequested: Navigation.back()
        onFullScreenToggled: view.toggleFullScreen()
        settingsOpen: view.settingsOpen
        // Nothing opens over the end card, as with `i`: the bars can be called over its veil,
        // and the panel opened from them was shut again by the card the same instant.
        onSettingsToggled: {
            if (!Reader.atTheEnd)
                view.settingsOpen = !view.settingsOpen
        }
        volumeOpen: view.volumeOpen
        onVolumeToggled: {
            if (!Reader.atTheEnd)
                view.volumeOpen = !view.volumeOpen
        }
        shortcutsOpen: view.shortcutsOpen
        onShortcutsToggled: view.toggleShortcuts()

        Behavior on opacity {
            enabled: view.animated

            NumberAnimation { duration: 120 }
        }
    }

    ReaderDock {
        id: dock

        width: Math.min(Widths.scale.size(640), view.width * 0.78)
        x: Math.round((view.width - dock.width) / 2)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.round(view.height * 0.016)
        rightToLeft: view.rightToLeft
        onForwardRequested: view.turn(1)
        onBackwardRequested: view.turn(-1)
        level: levelHeld.running ? ReaderCaptions.zoom(view.zoom) : ""
        // Up for the level as well as with the bars: the level is said there, and a reader
        // zooming with the bars away is the usual case.
        opacity: view.barsShown || levelHeld.running ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            enabled: view.animated

            NumberAnimation { duration: 120 }
        }
    }

    // Hung from `tune`, shifted left only as far as the window needs.
    ReaderSettings {
        id: card

        // Follows the window, never wider nor taller than the window leaves: in the owner's test
        // of 22/10/2026 on a window of 1920 it kept its drawn size beside a dock that had grown.
        // At half the dock's rate (`column`): its words no longer grow, and at the full rate
        // the card was a band of empty paper right of its pills.
        width: Math.min(view.width - 24, Widths.scale.column(640))
        x: Math.max(12, Math.min(view.width - card.width - 12, bar.settingsRight - card.width))
        y: bar.height + 8
        room: view.height - card.y - view.margin
        visible: view.settingsOpen && view.barsShown
        onCloseRequested: view.settingsOpen = false
    }

    // A card over the right of the pages, under the bar when the bars are up and above the
    // dock when it is, slid in from the edge it stands against. It used to take the right half
    // of the window and shrink the pages into the left one; tried by hand on 22/10/2026, a
    // page halved to make room for a card read worse than a page with a card over its edge,
    // and every opening and closing laid the book out again. The pages keep their size now,
    // and the card goes as it came.
    ReaderVolumePanel {
        id: panel

        x: view.width - panel.width - view.margin
        y: view.barsShown ? bar.height + 8 : view.margin
        // Its share of the window, within its drawn widths so scaled, and never more than two
        // fifths of a window that has room for the pages beside it: on a narrow one a card
        // that thin holds nothing, and it takes what it needs over the page.
        width: Math.min(Widths.scale.column(480),
                        Math.max(Widths.scale.column(320), view.width * 0.36),
                        Widths.band === Widths.Band.Narrow ? view.width : view.width * 0.4,
                        view.width - 2 * view.margin)
        height: (view.barsShown ? dock.y - 8 : view.height - view.margin) - panel.y
        opacity: view.volumeOpen ? 1 : 0
        visible: panel.opacity > 0
        onCloseRequested: view.volumeOpen = false

        // Slid with its fade, by the same amount: one animation, so the two never disagree.
        transform: Translate {
            x: (1 - panel.opacity) * 32
        }

        Behavior on opacity {
            enabled: view.animated

            NumberAnimation {
                duration: 200
                easing.type: Easing.OutCubic
            }
        }
    }

    // Over everything else, the bars included: a veil over the page with the bars bright above
    // it would read as two layers asking for attention. The bars' buttons still answer
    // through it (see `tappedAt`).
    ReaderShortcuts {
        id: keys

        anchors.fill: parent
        open: view.shortcutsOpen
        rightToLeft: view.rightToLeft
        onCloseRequested: view.shortcutsOpen = false
    }

    // A position the server would not record, said once and not at every page: offline,
    // every turn is refused for the same reason, and a bubble per page turned buried the book
    // under a column of identical failures. `Toasts` draws what
    // it is handed, so the merging is here — one bubble per reason for as long as this book
    // is open, and a new reason is a new bubble.
    Connections {
        target: Reader

        function onRefused(why) {
            if (why === view.lastRefusal)
                return
            view.lastRefusal = why
            Toasts.commandRefused(why)
        }

        // Kept once more: the next refusal, even for the same reason, is news again — offline,
        // back, offline said nothing the second time.
        function onRecorded() {
            view.lastRefusal = ""
        }
    }
}
