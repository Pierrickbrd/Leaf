// A small Leaf popup surface. Choice rows remain separate — LeafMenuItem owns their states —
// while this component owns the part every menu shares: paper, edge, spacing and elevation.

import QtQuick
import QtQuick.Controls
import Leaf

Menu {
    id: menu

    // OutsideParent and not the Menu default, which is OutsideOverlay: a menu is declared
    // inside the button that opens it, so a press there is *inside* its parent and closes
    // nothing. Left at the default, that press shut the menu before reaching the button, the
    // button then found it closed, and the release opened it again — one gesture, and the
    // menu never appeared to close.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    // Held off the window's edge as a page is, and padded as every menu and panel is: the
    // sort menu had 8, the filters 8 and the editions 4 (spacing audit, 23/10/2026).
    margins: Widths.space.gutter
    padding: Widths.space.compact
    overlap: 0

    delegate: LeafMenuItem { }

    // As wide as the widest row asks to be, and no narrower than the floor. `Menu` does not
    // do it for a style that draws its own background: its content width stayed at zero and
    // every menu was the background's constant, whatever its words — « Marquer toute la série
    // comme non lue » ended in « … ». Taken when the menu opens, because which rows are shown
    // depends on the rights of whoever opens it.
    property real widest: 0
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            widest + leftPadding + rightPadding)

    // When it turns visible and not in `aboutToShow`: Qt 6.4 has not yet made its rows visible
    // there, so every one was skipped and the menu stayed at the floor.
    onVisibleChanged: {
        if (!menu.visible)
            return
        let wide = 0
        for (let i = 0; i < menu.count; ++i) {
            const row = menu.itemAt(i)
            if (row && row.visible)
                wide = Math.max(wide, row.implicitWidth)
        }
        menu.widest = wide
    }

    background: Item {
        implicitWidth: Widths.scale.column(248)
        implicitHeight: Widths.scale.size(48)

        Rectangle {
            id: panel

            objectName: menu.objectName.length > 0 ? menu.objectName + "-panel" : ""
            anchors.fill: parent
            radius: Theme.buttonRadius
            color: Theme.surface
            border.color: Theme.rule
            border.width: 1

            CardLift { level: CardLift.Menu }
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 110 }
    }

    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80 }
    }
}
