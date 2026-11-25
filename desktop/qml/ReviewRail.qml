// The checking frame's tree, on its left: the whole drop, its universes, their series and the
// editions a series declares — and under it, the one sentence about what will be converted.
//
// A click on a node shows what it holds in the list and its own sheet; a click on its chevron
// folds it. From the keys, Enter and Space choose it, Left and Right fold and unfold it, as a
// tree of the desktop does.
//
// Once a role in the tree is read from the shape of the folders rather than said by a file,
// each line ends in its role (`RoleChip`) instead of its count, and a click there opens the
// menu of its roles; the box under the tree then says how a role is changed, and what it
// said before — what becomes of the ComicInfo, nothing said — goes beside the filters.
//
// The lines are drawn from `Review.rail` by **count**, each one reading its own entry: a
// model that is the list itself rebuilt every line on every change of the queue, and the line
// holding the keys was destroyed under them each time a value was typed on the sheet. A count
// moves only when a node is folded, and then `keep` puts the keys back on the line they were
// on.
//
// The tree is read once, here, and each line reads its entry from that: read by each line
// from `Review`, one change of the queue built the tree once per line. And not at all while
// the frame is shut — the queue changes all through a transfer nobody is checking.
//
// While an order is written the rail is the universe's instead: its name, its orders — one
// chosen as a tab is, from the keys or a click — and its series, the works a step may read.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Item {
    id: rail

    /// The node that must still hold the keys once the lines are rebuilt.
    property string keep: ""
    /// And whether its role chip holds them rather than the line: a role chosen from the
    /// keys leaves them on the chip it was chosen from.
    property bool keepOnChip: false
    /// Whether the line chosen takes the keys as soon as it is built (`takeKeys`). Lowered
    /// once a tree with lines has been built, whichever took them: left raised, it gave them
    /// to the next line chosen by a click, long after.
    property bool keepSelected: false

    onNodesChanged: {
        if (rail.keepSelected && rail.nodes.length > 0)
            Qt.callLater(() => rail.keepSelected = false)
    }

    /// The line that is the tree's stop of Tab, by its id: the last one the keys were on, or
    /// the node chosen while none was. At fifty series, a stop per line was fifty presses
    /// between the filters and the list; Up, Down, Home, End and the pages walk it instead.
    property string current: ""
    readonly property int stop: {
        let chosen = -1
        for (let at = 0; at < rail.nodes.length; ++at) {
            if (rail.nodes[at].id === rail.current)
                return at
            if (chosen < 0 && rail.nodes[at].selected === true)
                chosen = at
        }
        return Math.max(0, chosen)
    }

    /// The keys went with the line that held them, built again — a card read meanwhile
    /// changes the count of lines, and every line is built anew — and fell back on what holds
    /// the tree, nothing a reader chose.
    function keysLost(): bool {
        const held = rail.Window.activeFocusItem
        if (!held)
            return true
        for (let at = rail; at; at = at.parent) {
            if (at === held)
                return true
        }
        return false
    }

    /// The keys to the line at `index`, kept inside the tree.
    function goTo(index: int): void {
        const line = lines.itemAt(Math.max(0, Math.min(lines.count - 1, index)))
        if (line)
            line.forceActiveFocus(Qt.TabFocusReason)
    }
    /// The frame is a look at what already went: roles are shown, not changed.
    property bool readOnly: false

    /// An order is being written: the rail is the universe's.
    readonly property bool ordering: rail.visible && OrderEditor.open
    readonly property var nodes: rail.visible && !rail.ordering ? Review.rail : []
    /// Whether the lines end in their roles: only once one of them is a proposal. A drop
    /// that says everything keeps its counts, its roles being what its files say.
    readonly property bool roles: rail.visible && Review.railHint.length > 0
    /// While the role menu is open, the box says what its greyed lines are: the one moment
    /// somebody reads the box for that.
    readonly property string note: !rail.visible ? ""
                                 : rail.roles && roleMenu.visible ? ReviewCaptions.greyedRolesLabel
                                 : rail.roles ? Review.railHint : Review.railNote

    /// Once the lines are rebuilt, or were not: the line named `keep` took the keys back as
    /// it was built (`Component.onCompleted` below), and the name is no longer needed.
    function settle() {
        rail.keep = ""
        rail.keepOnChip = false
    }

    /// The keys to the line chosen: what the sheet held them on went with a regroup. While
    /// the cards it made are still read the tree has no line at all, so the line takes them
    /// as it is built (`keepSelected`).
    function takeKeys() {
        for (let at = 0; at < lines.count; ++at) {
            const line = lines.itemAt(at)
            if (line && rail.nodes[at]?.selected === true) {
                line.forceActiveFocus(Qt.OtherFocusReason)
                return
            }
        }
        rail.keepSelected = true
    }

    function choose(id: string) {
        rail.keepSelected = false
        rail.keep = id
        Review.view.select(id)
        Qt.callLater(rail.settle)
    }

    function fold(id: string) {
        rail.keep = id
        Review.view.toggle(id)
        Qt.callLater(rail.settle)
    }

    /// The menu of `id`'s roles under its chip, the node chosen as a click on it would.
    function askRole(chip: Item, id: string) {
        rail.choose(id)
        roleMenu.show(chip, id)
    }

    function giveRole(id: string, role: int) {
        rail.keep = id
        rail.keepOnChip = true
        Review.edits.setRole(id, role)
        Qt.callLater(rail.settle)
    }

    clip: true

    Text {
        id: caption

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Widths.space.row
        anchors.leftMargin: Widths.space.row + Widths.scale.size(10)
        textFormat: Text.PlainText
        text: !rail.visible ? "" : rail.ordering ? SheetCaptions.orderWords.universe
                                                 : Review.railTitle
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

        visible: !rail.ordering
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: caption.bottom
        anchors.bottom: next.top
        anchors.topMargin: Widths.space.tight
        anchors.leftMargin: Widths.space.row
        anchors.rightMargin: Widths.space.row
        anchors.bottomMargin: Widths.space.row
        contentHeight: column.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: column

            width: flick.width
            spacing: 1

            Repeater {
                id: lines

                model: rail.nodes.length

                delegate: Item {
                    id: line

                    required property int index
                    // A line past the end of a list that just shrank is read once more before
                    // it goes: an empty node rather than a TypeError for that one reading.
                    readonly property var node: rail.nodes[index] ?? ({ id: "", name: "", depth: 0,
                                                                        level: -1, count: 0 })
                    readonly property bool selected: line.node.selected === true
                    readonly property bool expandable: line.node.expandable === true

                    objectName: "review-node-" + index
                    width: column.width
                    height: Math.max(Widths.scale.fit(30), name.implicitHeight + 2 * Widths.scale.fit(7))
                    activeFocusOnTab: line.index === rail.stop

                    onActiveFocusChanged: {
                        if (line.activeFocus)
                            rail.current = line.node.id
                    }

                    Component.onCompleted: {
                        if (rail.keepSelected && line.selected) {
                            rail.keepSelected = false
                            line.forceActiveFocus(Qt.OtherFocusReason)
                            return
                        }
                        // Where the tree was scrolled, for the reason `ReviewList` gives.
                        if (rail.keep === "" && line.node.id === rail.current && rail.keysLost()) {
                            const scrolled = flick.contentY
                            line.forceActiveFocus(Qt.TabFocusReason)
                            flick.contentY = scrolled
                            return
                        }
                        if (rail.keep === "" || line.node.id !== rail.keep)
                            return
                        if (rail.keepOnChip && role.visible)
                            role.forceActiveFocus(Qt.TabFocusReason)
                        else
                            line.forceActiveFocus(Qt.TabFocusReason)
                    }

                    Accessible.role: Accessible.TreeItem
                    Accessible.name: line.node.name
                    Accessible.selected: line.selected

                    Keys.onPressed: event => {
                        const page = Math.max(1, Math.floor(flick.height / line.height))
                        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                                || event.key === Qt.Key_Enter) {
                            rail.choose(line.node.id)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down
                                   || event.key === Qt.Key_PageUp
                                   || event.key === Qt.Key_PageDown) {
                            const step = event.key === Qt.Key_Up ? -1
                                       : event.key === Qt.Key_Down ? 1
                                       : event.key === Qt.Key_PageUp ? -page : page
                            rail.goTo(line.index + step)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                            rail.goTo(event.key === Qt.Key_Home ? 0 : lines.count - 1)
                            event.accepted = true
                        } else if (line.expandable && ((event.key === Qt.Key_Left
                                                        && line.node.expanded === true)
                                                       || (event.key === Qt.Key_Right
                                                           && line.node.expanded !== true))) {
                            rail.fold(line.node.id)
                            event.accepted = true
                        }
                    }

                    HoverHandler {
                        id: pointer

                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: point => {
                            if (line.expandable && point.position.x < chevron.x + chevron.width
                                                                     + Widths.space.label)
                                rail.fold(line.node.id)
                            else
                                rail.choose(line.node.id)
                        }
                    }

                    // An archive no role reads is written in the alert's ink, as its chip, and
                    // lit in the alert's wash when it is the node shown: in the emerald, it
                    // looked like the one line that was fine.
                    readonly property bool unread: line.node.unread === true
                    readonly property color ink: line.unread ? Theme.alert
                                               : line.selected ? Theme.emerald : Theme.ink

                    Rectangle {
                        anchors.fill: parent
                        radius: Widths.scale.size(9)
                        color: line.selected ? (line.unread ? Theme.alertWash : Theme.emeraldWash)
                             : pointer.hovered ? Theme.rule : "transparent"
                    }

                    FocusRing {
                        objectName: line.objectName + "-focus"
                        inside: true
                        cornerRadius: Widths.scale.size(9)
                    }

                    FoldChevron {
                        id: chevron

                        // Ten a floor, four less than the frame drew: at fourteen, three levels
                        // down, a name had lost a third of its line to the step. The card's
                        // tree steps twenty because each of its names carries a second line.
                        x: line.node.depth * Widths.scale.size(10) + Widths.scale.size(10)
                        anchors.verticalCenter: parent.verticalCenter
                        visible: line.expandable
                        open: line.node.expanded === true
                        warm: pointer.hovered
                        tint: line.selected ? Theme.emerald : warm ? Theme.ink : Theme.inkFaint
                    }

                    Text {
                        id: name

                        objectName: line.objectName + "-name"
                        // No level icon before it, as the frame drew the tree: in a column a
                        // third of the window wide, the icon left « Dragon … » of the name.
                        anchors.left: chevron.right
                        anchors.leftMargin: Widths.space.label
                        anchors.right: role.visible ? role.left : count.left
                        anchors.rightMargin: Widths.space.label
                        anchors.verticalCenter: parent.verticalCenter
                        text: line.node.name
                        textFormat: Text.PlainText
                        color: line.ink
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(13)
                        font.weight: line.selected ? Font.DemiBold : Font.Normal
                        // On two lines beside a role chip rather than cut on one: the chip takes
                        // a third of the column, and « Terres d'Arran » ended at « T… ».
                        wrapMode: role.visible ? Text.WordWrap : Text.NoWrap
                        maximumLineCount: role.visible ? 2 : 1
                        elide: Text.ElideRight
                    }

                    Text {
                        id: count

                        anchors.right: parent.right
                        anchors.rightMargin: Widths.scale.size(10)
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !role.visible
                        textFormat: Text.PlainText
                        text: line.node.count
                        color: line.selected ? Theme.emerald : Theme.inkSoft
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.caption(12)
                    }

                    RoleChip {
                        id: role

                        objectName: "role-chip-" + line.node.id
                        anchors.right: parent.right
                        anchors.rightMargin: Widths.scale.size(8)
                        anchors.verticalCenter: parent.verticalCenter
                        visible: rail.roles && (line.node.role ?? -1) >= 0
                        node: line.node
                        readOnly: rail.readOnly
                        // Tab goes from the line the keys are on to its role, then out of the
                        // tree: a stop per chip was the tree's fifty stops again.
                        activeFocusOnTab: visible && !rail.readOnly && line.index === rail.stop
                        onAsked: rail.askRole(role, line.node.id)
                    }
                }
            }
        }
    }

    FollowsFocus {
        flick: flick
    }

    // ——— While an order is written ——————————————————————————————————————————————————

    Flickable {
        id: universe

        readonly property var words: SheetCaptions.orderWords
        readonly property var tabs: rail.ordering ? OrderEditor.tabs : []
        readonly property var works: rail.ordering ? OrderEditor.works : []

        visible: rail.ordering
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: caption.bottom
        anchors.bottom: parent.bottom
        anchors.topMargin: Widths.space.tight
        anchors.leftMargin: Widths.space.row
        anchors.rightMargin: Widths.space.row
        anchors.bottomMargin: Widths.space.row
        contentHeight: held.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        component Heading: Item {
            id: heading

            required property string label
            required property int count

            width: held.width
            height: Math.max(Widths.scale.fit(30), headingText.implicitHeight
                                                   + 2 * Widths.scale.fit(7))

            Accessible.role: Accessible.Heading
            Accessible.name: heading.label

            Text {
                id: headingText

                anchors.left: parent.left
                anchors.leftMargin: Widths.scale.size(10)
                anchors.right: headingCount.left
                // A long name of a universe stopped against its count.
                anchors.rightMargin: Widths.space.label
                anchors.verticalCenter: parent.verticalCenter
                text: heading.label
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.body(13)
                font.weight: Font.DemiBold
            }

            Text {
                id: headingCount

                anchors.right: parent.right
                anchors.rightMargin: Widths.scale.size(10)
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: heading.count
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
            }
        }

        Column {
            id: held

            width: universe.width
            spacing: 1

            Heading {
                objectName: "order-rail-universe"
                label: OrderEditor.universeName
                count: universe.works.length
            }

            Heading {
                label: universe.words.orders
                count: universe.tabs.length
            }

            Repeater {
                model: universe.tabs.length

                delegate: Item {
                    id: order

                    required property int index
                    readonly property var tab: universe.tabs[order.index]
                                               ?? ({ id: "", name: "", works: 0, chosen: false })

                    objectName: "order-rail-" + order.tab.id
                    width: held.width
                    height: Math.max(Widths.scale.fit(30), orderName.implicitHeight
                                                           + 2 * Widths.scale.fit(7))
                    activeFocusOnTab: true

                    Accessible.role: Accessible.TreeItem
                    Accessible.name: order.tab.name
                    Accessible.selected: order.tab.chosen

                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                                || event.key === Qt.Key_Enter) {
                            OrderEditor.choose(order.tab.id)
                            event.accepted = true
                        }
                    }

                    HoverHandler {
                        id: orderPointer

                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: OrderEditor.choose(order.tab.id)
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: Widths.scale.size(9)
                        color: order.tab.chosen ? Theme.emeraldWash
                             : orderPointer.hovered ? Theme.rule : "transparent"
                    }

                    FocusRing {
                        objectName: order.objectName + "-focus"
                        inside: true
                        cornerRadius: Widths.scale.size(9)
                    }

                    Text {
                        id: orderName

                        anchors.left: parent.left
                        anchors.leftMargin: Widths.scale.size(24)
                        anchors.right: orderWorks.left
                        anchors.rightMargin: Widths.space.label
                        anchors.verticalCenter: parent.verticalCenter
                        text: order.tab.name
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: order.tab.chosen ? Theme.emerald : Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(13)
                        font.weight: order.tab.chosen ? Font.DemiBold : Font.Normal
                    }

                    Text {
                        id: orderWorks

                        anchors.right: parent.right
                        anchors.rightMargin: Widths.scale.size(10)
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: order.tab.works
                        color: order.tab.chosen ? Theme.emerald : Theme.inkSoft
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.caption(12)
                    }
                }
            }

            Heading {
                label: universe.words.series
                count: universe.works.length
            }

            Repeater {
                model: universe.works

                delegate: Text {
                    required property string modelData

                    x: Widths.scale.size(24)
                    width: held.width - x - Widths.scale.size(10)
                    height: Math.max(Widths.scale.fit(26), implicitHeight)
                    verticalAlignment: Text.AlignVCenter
                    text: modelData
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(13)

                    Accessible.role: Accessible.StaticText
                    Accessible.name: modelData
                }
            }
        }
    }

    FollowsFocus {
        flick: universe
    }

    // « Revoir la suivante »: the next series that asks something, in the tree's order, one
    // press away rather than a hunt through fifty lines. Off when nothing is left to review.
    CardAction {
        id: next

        objectName: "review-next"
        anchors.left: parent.left
        anchors.leftMargin: Widths.space.row
        anchors.bottom: note.visible ? note.top : parent.bottom
        anchors.bottomMargin: Widths.space.row
        // Shown only when something is left to look at: greyed over a frame with nothing to
        // review, it named « the next one » to someone who had not been told there was a first.
        visible: !rail.ordering && Review.toReview > 0
        enabled: rail.visible
        label: ReviewCaptions.reviewNextLabel
        onTriggered: Review.view.reviewNext()
    }

    RoleMenu {
        id: roleMenu

        onChosen: (id, role) => rail.giveRole(id, role)
    }

    // What becomes of the ComicInfo, or that nothing is asked: once, under the tree, in the
    // quiet box every aside of the frame sits in.
    Rectangle {
        id: note

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Widths.space.row
        visible: rail.note.length > 0 && !rail.ordering
        height: noteText.implicitHeight + 2 * Widths.space.row
        radius: Widths.scale.size(10)
        color: Theme.onPaper
        border.color: Theme.rule
        border.width: 1

        Text {
            id: noteText

            objectName: "review-rail-note"
            anchors.fill: parent
            anchors.margins: Widths.space.row
            text: rail.note
            textFormat: Text.PlainText
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            lineHeight: 1.3
            wrapMode: Text.WordWrap
        }
    }
}
