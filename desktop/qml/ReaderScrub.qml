// How far into the volume, cut where each chapter begins.
//
// What no other reader can draw, because no other reader knows the chapters inside a volume:
// a mark at every `startPage`, emerald once it is behind the page on screen. A chapter that
// does not say where it begins has no mark — a mark guessed would be a jump to the wrong page.
//
// It fills the way the book reads. A manga's bar filling from the left would say the reader
// is near the end of a volume they just opened, read the way their eye goes.
//
// Sliding along it says, in the bubble the application uses for every hint, which chapter and
// which page are under the pointer, and lets go there on release — not on every page crossed,
// which would turn twenty pages and write where reading stopped for each of them.
//
// Under it, a line says the chapter being read on the side reading starts from, and where the
// volume ends on the other (spec §02) — mirrored with the bar in a manga, so « fin du tome » is
// always at the end the fill is going towards. A volume without chapters has no line: its two
// halves would be an empty name and « fin du tome », which the bar already shows by its end.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Leaf

Item {
    id: scrub

    property bool rightToLeft: false

    readonly property int count: Math.max(1, Reader.pageCount)
    /// The last page on screen: a pair has been read up to its second page.
    readonly property int reached: Math.max(Reader.firstPage, Reader.secondPage)
    /// The page under the pointer while it slides, −1 the rest of the time.
    property int held: -1
    readonly property int shown: scrub.held >= 0 ? scrub.held : scrub.reached

    /// Where along the track a fraction of the volume falls, measured from the side reading
    /// starts on.
    function xOf(fraction) {
        const along = Math.max(0, Math.min(1, fraction)) * track.width
        return scrub.rightToLeft ? track.width - along : along
    }

    /// The page whose end is nearest the pointer, because the end of a page is where the
    /// knob is drawn for it — the fill says « read up to here ». Taken as the page the pointer
    /// was inside, the knob ran up to a page ahead of the finger.
    function pageAt(x) {
        let fraction = track.width > 0 ? x / track.width : 0
        if (scrub.rightToLeft)
            fraction = 1 - fraction
        return Math.max(0, Math.min(scrub.count - 1, Math.round(fraction * scrub.count) - 1))
    }

    /// The chapter of the first page on screen, `null` when none says where it begins — the
    /// one the bar at the top names, by the same rule.
    readonly property var here: {
        const at = Reader.firstPage >= 0 ? Reader.chapterAt(Reader.firstPage) : -1
        return at >= 0 && at < Reader.chapters.length ? Reader.chapters[at] : null
    }
    readonly property bool captioned: Reader.chapters.length > 0

    function saidOf(page) {
        const at = Reader.chapterAt(page)
        const chapter = at >= 0 ? Reader.chapters[at] : null
        return ReaderCaptions.scrubbing(chapter ? chapter.label : "",
                                        chapter ? chapter.title : "", page)
    }


    objectName: "reader-scrub"
    implicitHeight: scrub.captioned ? band.height + caption.anchors.topMargin + caption.implicitHeight : band.height

    // The strip a hand slides along: the track and room round it for the knob and the press,
    // centred when there is no line under it.
    Item {
        id: band

        anchors.left: parent.left
        anchors.right: parent.right
        y: scrub.captioned ? 0 : (scrub.height - band.height) / 2
        height: Widths.scale.size(20)
    }

    Rectangle {
        id: track

        anchors.left: band.left
        anchors.right: band.right
        anchors.verticalCenter: band.verticalCenter
        height: Widths.scale.size(5)
        radius: height / 2
        color: Theme.onPaper

        Rectangle {
            objectName: "reader-scrub-fill"
            x: scrub.rightToLeft ? parent.width - width : 0
            width: parent.width * (scrub.shown + 1) / scrub.count
            height: parent.height
            radius: parent.radius
            color: Theme.emerald
        }

        Repeater {
            model: Reader.chapters

            Rectangle {
                id: mark

                required property var modelData

                readonly property bool past: mark.modelData.startPage <= scrub.reached

                visible: mark.modelData.startPage >= 0
                x: scrub.xOf(mark.modelData.startPage / scrub.count) - width / 2
                y: -3
                width: Widths.scale.size(2)
                height: Widths.scale.size(11)
                radius: width / 2
                color: mark.past ? Theme.emerald : Theme.rule
                opacity: mark.past ? 0.55 : 1
            }
        }

        Rectangle {
            id: knob

            x: scrub.xOf((scrub.shown + 1) / scrub.count) - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: Widths.scale.size(13)
            height: width
            radius: width / 2
            color: Theme.emerald
            border.color: Theme.surface
            border.width: Widths.scale.size(3)
        }
    }

    // A MouseArea and not a DragHandler, because what is wanted is the press itself — a
    // click on the bar goes to that page as much as a slide does — and the position while it
    // is held, both of which a MouseArea gives without a threshold to cross first.
    MouseArea {
        id: slide

        // The band and not the line under it: a press on a chapter's name is not a jump to
        // wherever under it the pointer happened to be.
        anchors.fill: band
        preventStealing: true
        cursorShape: Qt.PointingHandCursor
        onPressed: mouse => scrub.held = scrub.pageAt(mouse.x)
        onPositionChanged: mouse => {
            if (pressed)
                scrub.held = scrub.pageAt(mouse.x)
        }
        onReleased: {
            const page = scrub.held
            scrub.held = -1
            if (page >= 0)
                Reader.goToPage(page)
        }
        onCanceled: scrub.held = -1
    }

    LeafToolTip {
        objectName: "reader-scrub-tip"
        parent: track
        visible: scrub.held >= 0
        delay: 0
        timeout: -1
        x: knob.x + knob.width / 2 - implicitWidth / 2
        y: -implicitHeight - 12
        text: scrub.held >= 0 ? scrub.saidOf(scrub.held) : ""
        // A chapter's title is the server's, and text.
        textFormat: Text.PlainText
    }

    RowLayout {
        id: caption

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: band.bottom
        anchors.topMargin: Widths.scale.room(3)
        visible: scrub.captioned
        spacing: Widths.scale.room(12)
        layoutDirection: scrub.rightToLeft ? Qt.RightToLeft : Qt.LeftToRight

        // The label stronger than its title, as the bar's second line is not: here the label
        // is what the eye looks for. Two pieces of text kept in their own order whatever side
        // they stand on — mirrored, « Coup de sifflet · Chap.099 » read backwards.
        Row {
            Layout.maximumWidth: caption.width / 2
            Layout.alignment: Qt.AlignVCenter
            visible: scrub.here !== null

            Text {
                id: label

                objectName: "reader-scrub-chapter"
                text: scrub.here ? scrub.here.label : ""
                textFormat: Text.PlainText
                color: Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(11)
                font.weight: Font.Medium
            }

            Text {
                objectName: "reader-scrub-title"
                width: Math.min(implicitWidth, caption.width / 2 - label.width)
                text: ReaderCaptions.besideLabel(scrub.here ? scrub.here.label : "",
                                                 scrub.here ? scrub.here.title : "")
                textFormat: Text.PlainText
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(11)
                elide: Text.ElideRight
            }
        }

        Item {
            Layout.fillWidth: true
        }

        Text {
            objectName: "reader-scrub-end"
            Layout.maximumWidth: caption.width / 2
            Layout.alignment: Qt.AlignVCenter
            text: scrub.captioned
                  ? ReaderCaptions.volumeEnd(Reader.chapters[Reader.chapters.length - 1].label,
                                             Reader.chapters[Reader.chapters.length - 1].title)
                  : ""
            textFormat: Text.PlainText
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(11)
            elide: Text.ElideRight
        }
    }
}
