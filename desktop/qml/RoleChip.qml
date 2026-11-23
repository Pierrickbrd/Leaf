// What a folder or an archive was read as, at the end of its line in the checking frame's tree:
// « Étagère », « Série », « Non lu »… in the looks of `OriginChip`, by how surely it is known —
// said by a file, guessed from the shape, or a fault to mend — and a ▾ when another role can be
// chosen here.
//
// A click, or Space and Enter from the keys, asks for the menu of its roles; the tree opens it
// (`RoleMenu`), because one menu serves every line. A folder that declares itself still opens
// it: every other role is greyed there with the file that says it, and a chip that answered
// nothing to a click would not say why.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

OriginChip {
    id: chip

    /// The node of `Review.rail` this chip ends.
    required property var node
    /// Opened from the transfers, the frame is a look at what already went: nothing to change,
    /// and no menu offering it.
    property bool readOnly: false

    /// Whether a role other than this one can be chosen here — said by the tree's node, which
    /// asks it once per line: asked by each chip, a tree of fifty folders asked for fifty
    /// menus on every rebuild, and nothing counted them.
    readonly property bool changeable: !chip.readOnly && chip.node.changeable === true

    /// The menu is asked for, from a click or a key.
    signal asked()

    origin: chip.node.roleTone ?? 0
    label: chip.node.roleLabel ?? ""
    textSize: Widths.scale.caption(11)
    // The word's gap to the ▾ is a distance inside a box round a word, so it follows the word like
    // the 9 either side of it (`fit`): as `space.label` it doubled its growth with the window
    // and, at 1920, left twice the room between word and ▾ that there is before the word.
    trail: chip.changeable ? mark.width + Widths.scale.fit(8) : 0
    activeFocusOnTab: chip.visible && !chip.readOnly

    Accessible.role: chip.readOnly ? Accessible.StaticText : Accessible.ButtonMenu
    // The folder with its role: a tree of chips each called « Série » told nobody which one
    // had the keys. And the greyed roles with their reasons, which the menu's arrows skip.
    Accessible.name: chip.node.roleSpoken || chip.label
    Accessible.description: chip.readOnly ? "" : chip.node.rolesGreyed ?? ""
    Accessible.onPressAction: {
        if (!chip.readOnly)
            chip.asked()
    }

    Keys.onPressed: event => {
        if (!chip.readOnly && (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                               || event.key === Qt.Key_Enter)) {
            chip.asked()
            event.accepted = true
        }
    }

    HoverHandler {
        enabled: !chip.readOnly
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        enabled: !chip.readOnly
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: chip.asked()
    }

    FoldChevron {
        id: mark

        anchors.right: parent.right
        anchors.rightMargin: Widths.scale.fit(9)
        anchors.verticalCenter: parent.verticalCenter
        visible: chip.changeable
        open: true
        tint: chip.ink
    }

    FocusRing {
        objectName: chip.objectName + "-focus"
        cornerRadius: chip.height / 2
    }
}
