// One command in the menu the three dots open.
//
// Not `LeafMenuItem`, which is a radio row: the sort order is one thing among several and
// these are not — each of them does something and then the menu is gone. An icon rather than
// a mark, for the same reason.

import QtQuick
import QtQuick.Controls
import Leaf

MenuItem {
    id: command

    /// The file name of the icon, without its path — `assets/icons/<name>.svg`. Not `icon`:
    /// `AbstractButton` declares that one FINAL, and a component that overrides it does not
    /// fail to compile, it fails to exist — which takes every screen that draws it with it.
    required property string glyph
    /// Painted in the alert colour, text and icon both. One entry at most per menu: a list
    /// where two things are red is a list where neither is.
    property bool dangerous: false

    // As `LeafMenuItem`: a row of words, its height from them, `label` from glyph to word.
    //
    // Wide enough for its words when it can be: a constant 248 cut « Marquer toute la série
    // comme non lue » to « … comme n… » in a window with room for twice that, because a menu
    // is as wide as its widest row and none of them asked for more. The floor keeps short
    // menus one width; the window's margins, which a popup respects, are the only ceiling.
    implicitWidth: Math.max(Widths.scale.column(248),
                            leftPadding + Widths.scale.icon(20) + spacing + said.implicitWidth
                            + rightPadding)
    implicitHeight: Math.max(Widths.scale.size(36), said.implicitHeight + Widths.space.row)
    leftPadding: Widths.scale.size(10)
    rightPadding: Widths.scale.size(10)
    topPadding: 0
    bottomPadding: 0
    spacing: Widths.space.label

    font.family: Theme.textFamily
    font.pixelSize: Widths.scale.body(13)

    Accessible.role: Accessible.MenuItem
    Accessible.name: text

    // Hidden is not enough for a menu's keys: Down landed on « Réimporter » and « Supprimer »,
    // hidden from a profile without the right (audit, 25/10/2026). What is not shown cannot
    // be reached.
    enabled: visible

    contentItem: Item {
        Glyph {
            id: mark

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            side: Widths.scale.icon(20)
            source: "assets/icons/" + command.glyph + ".svg"
            tint: command.dangerous ? Theme.alert
                                    : (command.enabled ? Theme.inkSoft : Theme.inkFaint)
        }

        Text {
            id: said

            anchors.left: mark.right
            anchors.leftMargin: command.spacing
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: command.text
            color: !command.enabled ? Theme.inkFaint
                                    : (command.dangerous ? Theme.alert : Theme.ink)
            font: command.font
            elide: Text.ElideRight
        }
    }

    background: Rectangle {
        radius: 8
        color: command.highlighted ? Theme.onPaper : "transparent"
    }
}
