// A small Leaf popup surface. Choice rows remain separate — LeafMenuItem owns their states —
// while this component owns the part every menu shares: paper, edge, spacing and elevation.

import QtQuick
import QtQuick.Controls
import Leaf

Menu {
    id: menu

    margins: 10
    padding: 8
    overlap: 0

    delegate: LeafMenuItem { }

    background: Item {
        implicitWidth: 286
        implicitHeight: 48

        Rectangle {
            x: 2
            y: 6
            width: parent.width
            height: parent.height
            radius: Theme.buttonRadius
            color: "#000000"
            opacity: Theme.dark ? 0.52 : 0.16
        }

        Rectangle {
            x: 1
            y: 2
            width: parent.width
            height: parent.height
            radius: Theme.buttonRadius
            color: "#000000"
            opacity: Theme.dark ? 0.28 : 0.08
        }

        Rectangle {
            id: panel

            objectName: menu.objectName.length > 0 ? menu.objectName + "-panel" : ""
            anchors.fill: parent
            radius: Theme.buttonRadius
            color: Theme.surface
            border.color: Theme.rule
            border.width: 1
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 110 }
    }

    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80 }
    }
}
