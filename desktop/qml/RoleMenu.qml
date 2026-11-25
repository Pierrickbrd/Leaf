// The roles a folder or an archive of the checking frame can take, under the chip that opened
// it: each with what it is, or why it is greyed here — a role greyed without its reason reads
// as a menu that is broken — the one in force marked, those that can be chosen first and the
// greyed ones after a rule.
//
// A `LeafMenu`, so a popup in the window's top layer and not a block inside the tree: the tree
// clips at its edge, 210 wide at most, and the menu's reasons ask for 290 — inside the column,
// every one of them would lose its end. Opened under the chip, it spills over the list beside,
// stays in the window by the menu's margins, and is as wide as its widest line — narrowed by
// the window alone, the reasons then wrap rather than end in « … ».
//
// One menu for the whole tree: `show` hands it the node and the chip. Up and Down walk it,
// passing over what is greyed; Enter chooses; Escape shuts it — and the keys go back to the
// chip either way, since the tree is where the next key is aimed.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

LeafMenu {
    id: menu

    /// The node whose roles are listed.
    property string nodeId: ""
    /// The chip it was opened from, given the keys back once the menu is shut.
    property Item opener: null
    /// `Review.edits.roleOptions(nodeId)`, those that can be chosen first.
    property var options: []

    /// A role that can be chosen was chosen.
    signal chosen(string nodeId, int role)

    function show(chip: Item, id: string) {
        const all = Review.edits.roleOptions(id)
        menu.nodeId = id
        menu.opener = chip
        menu.options = all.filter(one => one.possible === true)
                          .concat(all.filter(one => one.possible !== true))
        // On the role in force, so that the first arrow moves from where the chip stands.
        const current = menu.options.findIndex(one => one.chosen === true)
        menu.startAt = current >= 0 && menu.options[current].possible === true ? current : -1
        menu.popup(chip, 0, chip.height + Widths.space.tight)
    }

    objectName: "role-menu"

    /// Where the first arrow starts. Given once the menu is open: Qt 6.10 puts the index back to
    /// nothing as the menu opens, and 6.4 reports a binding loop on it if it is set in the call
    /// that opens the menu.
    property int startAt: -1

    onOpened: menu.currentIndex = menu.startAt

    onClosed: {
        if (menu.opener)
            menu.opener.forceActiveFocus(Qt.PopupFocusReason)
        menu.opener = null
    }

    Instantiator {
        model: menu.options

        delegate: LeafMenuItem {
            id: option

            required property var modelData
            required property int index

            readonly property bool possible: option.modelData.possible === true
            /// The first greyed line, after those that can be chosen: the rule goes over it.
            readonly property bool firstGreyed: !option.possible && option.index > 0
                                                && menu.options[option.index - 1]?.possible === true

            objectName: "role-option-" + option.modelData.role
            text: option.modelData.label
            enabled: option.possible
            checked: option.modelData.chosen === true
            font.pixelSize: Widths.scale.body(13)
            topPadding: option.firstGreyed ? Widths.space.compact : 0
            implicitHeight: Math.max(Widths.scale.size(46),
                                     option.topPadding + said.implicitHeight
                                     + 2 * Widths.space.row)

            Accessible.role: Accessible.MenuItem
            Accessible.name: option.text
            Accessible.description: option.modelData.detail

            onTriggered: menu.chosen(menu.nodeId, option.modelData.role)

            contentItem: Item {
                id: said

                implicitWidth: Math.max(name.implicitWidth, detail.implicitWidth)
                implicitHeight: name.implicitHeight + 1 + detail.implicitHeight

                Text {
                    id: name

                    objectName: option.objectName + "-label"
                    // The two lines centred together in the row, which is taller than they are.
                    y: Math.max(0, (parent.height - said.implicitHeight) / 2)
                    width: parent.width
                    text: option.text
                    textFormat: Text.PlainText
                    color: !option.enabled ? Theme.inkFaint
                         : option.checked ? Theme.emerald : Theme.ink
                    font.family: Theme.textFamily
                    font.pixelSize: option.font.pixelSize
                    font.weight: option.checked ? Font.DemiBold : Font.Normal
                    wrapMode: Text.Wrap
                }

                Text {
                    id: detail

                    objectName: option.objectName + "-detail"
                    anchors.top: name.bottom
                    anchors.topMargin: 1
                    width: parent.width
                    text: option.modelData.detail ?? ""
                    textFormat: Text.PlainText
                    color: !option.enabled ? Theme.inkFaint
                         : option.checked ? Theme.emerald : Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(11)
                    lineHeight: 1.2
                    wrapMode: Text.Wrap
                }
            }

            Rectangle {
                visible: option.firstGreyed
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: Widths.space.compact
                anchors.rightMargin: Widths.space.compact
                height: 1
                color: Theme.rule
            }
        }

        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
}
