// One command in the menu the three dots open.
//
// Not `LeafMenuItem`, which is a radio row: the sort order is one thing among several and
// these are not — each of them does something and then the menu is gone. An icon rather than
// a mark, for the same reason.

import QtQuick
import QtQuick.Controls
import Qt5Compat.GraphicalEffects
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

    implicitWidth: 286
    implicitHeight: 42
    leftPadding: 14
    rightPadding: 14
    topPadding: 0
    bottomPadding: 0
    spacing: 12

    font.family: Theme.textFamily
    font.pixelSize: 14

    Accessible.role: Accessible.MenuItem
    Accessible.name: text

    contentItem: Item {
        Image {
            id: mark

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 20
            height: 20
            source: "assets/icons/" + command.glyph + ".svg"
            sourceSize.width: 20
            sourceSize.height: 20
            fillMode: Image.PreserveAspectFit
            visible: false
        }

        // Tinted rather than shipped in a colour: one file per icon, and it follows the
        // theme like every other glyph in this client.
        ColorOverlay {
            anchors.fill: mark
            source: mark
            color: command.dangerous ? Theme.alert
                                     : (command.enabled ? Theme.inkSoft : Theme.inkFaint)
        }

        Text {
            anchors.left: mark.right
            anchors.leftMargin: command.spacing
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: command.text
            color: !command.enabled ? Theme.inkFaint
                                    : (command.dangerous ? Theme.alert : Theme.ink)
            font: command.font
            elide: Text.ElideRight
        }
    }

    background: Rectangle {
        radius: Theme.buttonRadius - 2
        color: command.highlighted ? Theme.onBar : "transparent"
    }
}
