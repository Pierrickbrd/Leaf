// The checking frame's list, in its middle: what the node chosen in the tree holds, grouped —
// to review, ready, identical — each group under a line that counts it.
//
// A line that is a series goes down into it, as its node in the tree would; a line that is a
// file shows its sheet and the list stays where it was, since a file holds nothing to list.
// The identical group is folded unless asked for, and its header says how many and what they
// spare.
//
// Drawn by count for the reason `ReviewRail` gives. Going down into a series changes the
// count, and the keys then go to the first line of what was opened rather than to nothing.
//
// Read once, here, and not by each line: two readings a line, and one change of the queue
// built a list of a thousand chapters two thousand times. Not at all while the frame is shut.
//
// A series that can still be corrected has a box before its name, and « Tout » over the list
// ticks every one shown. The list is one stop of Tab, the line last reached: at fifty series,
// fifty stops between the tree and the sheet were fifty presses to cross it. Up, Down, Home,
// End, Page Up and Page Down walk it, Enter opens a line, Space ticks it, and Maj with an
// arrow ticks every line it passes.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Item {
    id: list

    /// The line that must still hold the keys once the lines are rebuilt; when it is gone —
    /// a series opened — the first line of the new list takes them.
    property string keep: ""

    readonly property var rows: list.visible ? Review.rows : []

    /// The line that is the list's stop of Tab, by its id: the last one the keys were on, or
    /// the first line while none was. It outlives the lines being built again.
    property string current: ""
    /// Where a run of Maj and an arrow began.
    property string anchor: ""
    /// Whether the keys are being moved by such a run, which keeps its anchor.
    property bool running: false
    readonly property int stop: {
        let first = -1
        for (let at = 0; at < list.rows.length; ++at) {
            const row = list.rows[at]
            if (row.header === true)
                continue
            if (row.id === list.current)
                return at
            if (first < 0)
                first = at
        }
        return first
    }
    /// Whether any line can be ticked: then « Tout » is drawn, and the names leave room for
    /// the boxes before them.
    readonly property bool tickable: list.rows.some(row => row.checkable === true)

    /// The keys went with the line that held them, rebuilt, and fell back on what holds the
    /// list — nothing a reader chose.
    function keysLost(): bool {
        const held = list.Window.activeFocusItem
        if (!held)
            return true
        for (let at = list; at; at = at.parent) {
            if (at === held)
                return true
        }
        return false
    }

    /// The line `step` lines after `from`, headers passed over, kept inside the list.
    function lineFrom(from: int, step: int): int {
        let found = from
        let left = Math.abs(step)
        const toward = step < 0 ? -1 : 1
        for (let at = from + toward; at >= 0 && at < list.rows.length && left > 0;
             at += toward) {
            if (list.rows[at].header === true)
                continue
            found = at
            --left
        }
        return found
    }

    /// The keys to the line at `index`; with `ticking`, every line from where the run began
    /// to it is ticked.
    function goTo(index: int, ticking: bool): void {
        const line = lines.itemAt(index)
        if (!line || line.header)
            return
        const id = line.row.id
        if (ticking) {
            if (list.anchor === "")
                list.anchor = list.current
            Review.ticks.tickRange(list.anchor, id)
        }
        list.current = id
        if (!ticking)
            list.anchor = id
        list.running = ticking
        line.forceActiveFocus(Qt.TabFocusReason)
        list.running = false
    }

    /// Once the lines are rebuilt, or were not. The line named `keep` took the keys back as it
    /// was built (`Component.onCompleted` below); when no line is it, the keys went nowhere
    /// and the first line of the new list takes them.
    function settle() {
        if (list.keep === "")
            return
        list.keep = ""
        for (let at = list.Window.activeFocusItem; at; at = at.parent) {
            if (at === list)
                return
        }
        for (let at = 0; at < lines.count; ++at) {
            const one = lines.itemAt(at)
            if (one && one.objectName.startsWith("review-line-")) {
                one.forceActiveFocus(Qt.TabFocusReason)
                return
            }
        }
    }

    function choose(id: string) {
        list.current = id
        list.anchor = id
        list.keep = id
        Review.view.select(id)
        Qt.callLater(list.settle)
    }

    // ——— Putting a volume in another place ——————————————————————————————————————
    //
    // A volume dragged by its grip goes where it is dropped, among the volumes of its own
    // group and not out of them, and its number follows: the numbers the group holds stay,
    // who holds which follows the order (`Review.edits.renumber`). The others make room as it
    // passes — each moves by the height of the one dragged, and settles with it — so the list
    // is never seen with a hole or a double. Dropped, it slides into its place before the
    // numbers are written, since the list is rebuilt in the new order and a line that jumped
    // there was a line that blinked.

    /// The line the dragged one would take if let go now.
    property int dropAt: -1

    /// The first and last line of the run of draggable lines that holds `index`.
    function runAround(index: int): var {
        let first = index
        let last = index
        while (first > 0 && list.rows[first - 1].draggable === true)
            --first
        while (last < list.rows.length - 1 && list.rows[last + 1].draggable === true)
            ++last
        return [first, last]
    }

    /// Follows the pointer, kept inside its run, and makes room where it would land.
    function moveTo(index: int, dy: real): void {
        const me = lines.itemAt(index)
        const [first, last] = list.runAround(index)
        const top = lines.itemAt(first)
        const bottom = lines.itemAt(last)
        if (!me || !top || !bottom)
            return
        const lowest = top.y - me.y
        const highest = bottom.y + bottom.height - me.y - me.height
        me.dragY = Math.max(lowest, Math.min(highest, dy))
        const middle = me.y + me.height / 2 + me.dragY
        let target = index
        for (let at = first; at <= last; ++at) {
            const one = lines.itemAt(at)
            if (one && middle >= one.y && middle < one.y + one.height)
                target = at
        }
        list.dropAt = target
        for (let at = first; at <= last; ++at) {
            const one = lines.itemAt(at)
            if (!one || at === index)
                continue
            one.shift = target > index && at > index && at <= target ? -me.height
                      : target < index && at >= target && at < index ? me.height : 0
        }
    }

    /// Slides the dragged line into its place; `commit` follows once it is there.
    function release(index: int): void {
        const me = lines.itemAt(index)
        if (!me)
            return
        let slot = 0
        for (let at = Math.min(index + 1, list.dropAt); at <= list.dropAt && list.dropAt > index; ++at)
            slot += lines.itemAt(at).height
        for (let at = list.dropAt; at < index && list.dropAt < index; ++at)
            slot -= lines.itemAt(at).height
        me.settleTo(slot)
    }

    /// Writes the order the drag made: the ids of the run with the dragged one moved.
    function commit(index: int): void {
        const [first, last] = list.runAround(index)
        const ids = []
        for (let at = first; at <= last; ++at)
            ids.push(list.rows[at].id)
        const moved = ids.splice(index - first, 1)[0]
        ids.splice(list.dropAt - first, 0, moved)
        for (let at = first; at <= last; ++at) {
            const one = lines.itemAt(at)
            if (one) {
                one.shift = 0
                one.dragY = 0
                one.lifted = false
            }
        }
        if (list.dropAt !== index)
            Review.edits.renumber(ids)
        list.dropAt = -1
    }

    clip: true

    // « Tout », over the lines it ticks: a stop of its own, before the list.
    LeafCheck {
        id: all

        objectName: "review-tick-all"
        anchors.left: parent.left
        anchors.leftMargin: Widths.space.row + Widths.scale.size(10)
        anchors.verticalCenter: caption.verticalCenter
        visible: list.tickable
        activeFocusOnTab: visible
        snug: true
        label: ReviewCaptions.tickAllLabel
        side: Widths.scale.size(16)
        checked: Review.ticks.tickState === 2
        partly: Review.ticks.tickState === 1
        onToggled: wanted => Review.ticks.tickShown(wanted)

        Accessible.description: Review.ticks.tickedLabel
    }

    Text {
        id: caption

        objectName: "review-list-title"
        anchors.left: all.visible ? all.right : parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Widths.space.row
        // On the edge of the lines' own text — the list's margin, then the line's inset — as
        // `ReviewRail`'s title is: at the card's 16 it stood four pixels left of every name
        // under it, and four right of the chips at the other end.
        anchors.leftMargin: all.visible ? Widths.space.row
                                        : Widths.space.row + Widths.scale.size(10)
        anchors.rightMargin: Widths.space.row + Widths.scale.size(10)
        horizontalAlignment: all.visible ? Text.AlignRight : Text.AlignLeft
        text: list.visible ? Review.listTitle : ""
        textFormat: Text.PlainText
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(11)
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: Widths.scale.letters(0.6)
        elide: Text.ElideRight
    }

    Flickable {
        id: flick

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: caption.bottom
        anchors.bottom: parent.bottom
        anchors.topMargin: Widths.space.compact
        anchors.leftMargin: Widths.space.row
        anchors.rightMargin: Widths.space.row
        objectName: "review-list-flick"
        // Held still while a volume is carried: the list scrolling under the hand that drags.
        interactive: list.dropAt < 0
        contentHeight: column.height + Widths.space.row
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: column

            width: flick.width

            Repeater {
                id: lines

                model: list.rows.length

                delegate: Item {
                    id: line

                    required property int index
                    // As `ReviewRail`'s node: read once more past the end of a list that shrank.
                    readonly property var row: list.rows[index] ?? ({ header: true, text: "" })
                    readonly property bool header: line.row.header === true
                    /// Where the line stands while others are dragged past it, and while it is.
                    property real shift: 0
                    property real dragY: 0
                    property bool lifted: false
                    readonly property bool draggable: !line.header && line.row.draggable === true

                    function settleTo(slot: real): void {
                        settling.to = slot
                        settling.start()
                    }

                    z: line.lifted ? 2 : 0
                    transform: Translate {
                        y: line.lifted ? line.dragY : line.shift

                        Behavior on y {
                            enabled: !line.lifted
                            NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                        }
                    }

                    NumberAnimation {
                        id: settling

                        target: line
                        property: "dragY"
                        duration: 150
                        easing.type: Easing.OutCubic
                        onFinished: list.commit(line.index)
                    }

                    objectName: header ? "review-group-" + index : "review-line-" + index
                    width: column.width
                    // Whole pixels, every line: at a fractional scale the lines stood on halves
                    // and the one-pixel rule between them was drawn thick on some and thin on
                    // others, by where it happened to fall.
                    height: Math.round(line.header ? group.height + 2 * Widths.space.row
                                                   : Math.max(Widths.scale.fit(44),
                                                              words.implicitHeight
                                                              + 2 * Widths.scale.fit(9)))
                    readonly property bool checkable: line.row.checkable === true
                    activeFocusOnTab: !line.header && line.index === list.stop

                    onActiveFocusChanged: {
                        if (!line.activeFocus || line.header)
                            return
                        // Reached by Tab or a click, a run of Maj begins here — even on the
                        // line it last ended on: kept from the run before, the anchor ticked
                        // lines from a place the keys had left long ago.
                        list.current = line.row.id
                        if (!list.running)
                            list.anchor = line.row.id
                    }

                    // The keys back on the line that held them, built again — a card read
                    // meanwhile adds a line, and every line is built anew. The list stays
                    // where it was: the line is not laid out yet, and `FollowsFocus` read its
                    // place as the top and scrolled the list back to it.
                    Component.onCompleted: {
                        if (line.header)
                            return
                        if ((list.keep !== "" && line.row.id === list.keep)
                                || (line.row.id === list.current && list.keysLost())) {
                            const scrolled = flick.contentY
                            line.forceActiveFocus(Qt.TabFocusReason)
                            flick.contentY = scrolled
                        }
                    }

                    Accessible.role: line.header ? Accessible.Heading : Accessible.ListItem
                    Accessible.name: line.header ? line.row.text : line.row.name
                    Accessible.description: line.header ? "" : line.row.detail
                    Accessible.selected: line.row.selected === true
                    Accessible.checkable: line.checkable
                    Accessible.checked: line.row.checked === true

                    Keys.onPressed: event => {
                        if (line.header)
                            return
                        const ticking = (event.modifiers & Qt.ShiftModifier) !== 0
                        const page = Math.max(1, Math.floor(flick.height / line.height))
                        switch (event.key) {
                        case Qt.Key_Return:
                        case Qt.Key_Enter:
                            list.choose(line.row.id)
                            break
                        case Qt.Key_Space:
                            if (event.isAutoRepeat)
                                break
                            if (line.checkable)
                                Review.ticks.tick(line.row.id, line.row.checked !== true)
                            else
                                list.choose(line.row.id)
                            break
                        case Qt.Key_Up:
                            list.goTo(list.lineFrom(line.index, -1), ticking)
                            break
                        case Qt.Key_Down:
                            list.goTo(list.lineFrom(line.index, 1), ticking)
                            break
                        case Qt.Key_PageUp:
                            list.goTo(list.lineFrom(line.index, -page), ticking)
                            break
                        case Qt.Key_PageDown:
                            list.goTo(list.lineFrom(line.index, page), ticking)
                            break
                        case Qt.Key_Home:
                            list.goTo(list.lineFrom(-1, 1), ticking)
                            break
                        case Qt.Key_End:
                            list.goTo(list.lineFrom(list.rows.length, -1), ticking)
                            break
                        default:
                            return
                        }
                        event.accepted = true
                    }

                    // ——— A group's header ———————————————————————————————————————————

                    Rectangle {
                        id: group

                        visible: line.header
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        // Air under the header too: the first line sat against it, and the two
                        // read as one block.
                        anchors.bottomMargin: Widths.space.row
                        height: Math.max(Widths.scale.fit(30),
                                         groupText.implicitHeight + 2 * Widths.scale.fit(8))
                        radius: Widths.scale.size(8)
                        color: Theme.onPaper

                        Text {
                            id: groupText

                            anchors.left: parent.left
                            anchors.leftMargin: Widths.scale.size(10)
                            anchors.right: show.visible ? show.left : parent.right
                            anchors.rightMargin: Widths.scale.size(10)
                            anchors.verticalCenter: parent.verticalCenter
                            text: line.header ? line.row.text : ""
                            textFormat: Text.PlainText
                            color: Theme.inkSoft
                            font.family: Theme.textFamily
                            font.pixelSize: Widths.scale.caption(11)
                            font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: Widths.scale.letters(0.6)
                            elide: Text.ElideRight
                        }

                        LeafTextAction {
                            id: show

                            objectName: line.objectName + "-action"
                            anchors.right: parent.right
                            anchors.rightMargin: Widths.scale.size(4)
                            anchors.verticalCenter: parent.verticalCenter
                            scaled: true
                            quiet: true
                            visible: line.header && line.row.action !== undefined
                                     && line.row.action.length > 0
                            activeFocusOnTab: visible
                            label: visible ? line.row.action : ""
                            onTriggered: Review.view.toggleIdentical()
                        }
                    }

                    // ——— A line ————————————————————————————————————————————————————

                    HoverHandler {
                        id: pointer

                        enabled: !line.header
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        enabled: !line.header
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: list.choose(line.row.id)
                    }

                    Rectangle {
                        visible: !line.header
                        anchors.fill: parent
                        radius: Widths.scale.size(10)
                        color: line.row.selected === true ? Theme.onBar
                             : pointer.hovered ? Theme.onPaper : "transparent"
                    }

                    FocusRing {
                        objectName: line.objectName + "-focus"
                        inside: true
                        cornerRadius: Widths.scale.size(10)
                    }

                    // The rule between two lines, and none over the first under its header
                    // nor round the one that is lit: a rule through a lit band cuts it in two.
                    //
                    // Short of both edges by the wash's radius, and gone when the line above is
                    // the one lit as well: drawn the full width it ran on past the rounded
                    // corners of the hovered or selected line, a straight bar sticking out of
                    // either side of a soft shape, as it did in the list of a series' volumes.
                    Rectangle {
                        visible: !line.header && line.index > 0
                                 && list.rows[line.index - 1]?.header === false
                                 && line.row.selected !== true
                                 && list.rows[line.index - 1]?.selected !== true
                                 && !pointer.hovered
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Widths.scale.size(10)
                        anchors.rightMargin: Widths.scale.size(10)
                        anchors.top: parent.top
                        height: 1
                        color: Theme.rule
                    }

                    // The box of a series that can still be corrected: the mark alone, the
                    // name beside it saying what it ticks.
                    LeafCheck {
                        id: box

                        objectName: "review-tick-" + line.index
                        anchors.left: parent.left
                        anchors.leftMargin: Widths.scale.size(10)
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !line.header && line.checkable
                        snug: true
                        label: ""
                        side: Widths.scale.size(16)
                        checked: line.row.checked === true
                        onToggled: wanted => Review.ticks.tick(line.row.id, wanted)

                        Accessible.name: line.header ? "" : line.row.name
                        Accessible.description: Review.ticks.tickedLabel
                    }

                    Column {
                        id: words

                        visible: !line.header
                        anchors.left: grip.visible ? grip.right : parent.left
                        anchors.leftMargin: grip.visible ? Widths.space.label
                                            : list.tickable
                                            ? Widths.scale.size(10) + Widths.scale.size(16)
                                              + Widths.space.label
                                            : Widths.scale.size(10)
                        // The names run to the edge of the line when there is no chip: an unseen
                        // chip still held its width, and the names of every line that lacked
                        // nothing stopped thirty pixels short of it.
                        anchors.right: flip.visible ? flip.left
                                                    : chip.visible ? chip.left : parent.right
                        anchors.rightMargin: chip.visible || flip.visible ? Widths.space.label
                                                                          : Widths.scale.size(10)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1

                        Text {
                            objectName: line.objectName + "-name"
                            width: parent.width
                            text: line.header ? "" : line.row.name
                            textFormat: Text.PlainText
                            color: Theme.ink
                            font.family: Theme.textFamily
                            font.pixelSize: Widths.scale.body(13)
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Text {
                            objectName: line.objectName + "-detail"
                            width: parent.width
                            text: line.header ? "" : line.row.detail
                            textFormat: Text.PlainText
                            // A file that will not be read says it in the alert's ink, as
                            // its chip: in the quiet ink, it read like any file's size.
                            color: line.row.alarming === true ? Theme.alert : Theme.inkSoft
                            font.family: Theme.textFamily
                            font.pixelSize: Widths.scale.caption(12)
                            elide: Text.ElideMiddle
                        }
                    }

                    // Whether the volume of a reimport is replaced: the line's own decision.
                    LeafSwitch {
                        id: flip

                        objectName: "review-switch-" + (line.header ? "" : line.row.id)
                        visible: !line.header && line.row.switchable === true
                        anchors.right: chip.left
                        anchors.rightMargin: Widths.space.row
                        anchors.verticalCenter: parent.verticalCenter
                        label: line.header ? "" : line.row.name
                        on: !line.header && line.row.on === true
                        onToggled: wanted => Review.edits.setSwitch(line.row.id, wanted)
                    }

                    // The grip a volume is dragged by: its numbers follow the order it is left in.
                    // Only the grip drags, so that a press on the line still opens it.
                    Item {
                        id: grip

                        objectName: line.objectName + "-grip"
                        visible: line.draggable
                        anchors.left: box.visible ? box.right : parent.left
                        anchors.leftMargin: box.visible ? Widths.space.label : Widths.scale.size(6)
                        anchors.verticalCenter: parent.verticalCenter
                        width: Widths.scale.size(24)
                        height: Widths.scale.size(28)

                        Accessible.role: Accessible.Button
                        Accessible.name: ReviewCaptions.moveVolumeLabel

                        Glyph {
                            anchors.centerIn: parent
                            source: "assets/icons/drag_indicator.svg"
                            side: Widths.scale.size(18)
                            tint: dragger.active || hover.hovered ? Theme.ink : Theme.inkFaint
                        }

                        HoverHandler {
                            id: hover

                            cursorShape: dragger.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                        }

                        DragHandler {
                            id: dragger

                            target: null
                            xAxis.enabled: false
                            // The list is a Flickable, and a press that moves up or down is
                            // its own before it is the grip's: the grip scrolled the volumes
                            // instead of lifting one, on most tries.
                            grabPermissions: PointerHandler.CanTakeOverFromAnything
                            onActiveChanged: {
                                if (active) {
                                    line.lifted = true
                                    list.dropAt = line.index
                                } else if (line.lifted) {
                                    list.release(line.index)
                                }
                            }
                            onTranslationChanged: {
                                if (active)
                                    list.moveTo(line.index, translation.y)
                            }
                        }
                    }

                    OriginChip {
                        id: chip

                        objectName: line.objectName + "-chip"
                        // Only on a line that lacks something: one that lacks nothing is
                        // worth no word, where it once wore « Écrit » like every other.
                        visible: !line.header && label.length > 0
                        anchors.right: parent.right
                        anchors.rightMargin: Widths.scale.size(10)
                        anchors.verticalCenter: parent.verticalCenter
                        origin: line.header ? 0 : line.row.origin
                        label: line.header ? "" : line.row.chip
                    }

                }
            }
        }
    }

    FollowsFocus {
        flick: flick
    }
}
