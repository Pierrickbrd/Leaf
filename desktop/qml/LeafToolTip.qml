// The application's one tooltip: the same warm surface, type and restrained elevation as
// everything it points at. Qt's Basic tooltip is deliberately neutral; leaving it in place
// makes the first hover introduce a third palette into the window.

import QtQuick
import QtQuick.Controls
import Leaf

ToolTip {
    id: tip

    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: parent ? parent.height + 8 : 0

    margins: 8
    leftPadding: 12
    rightPadding: 12
    topPadding: 8
    bottomPadding: 8
    delay: 450
    timeout: 4000

    font.family: Theme.textFamily
    font.pixelSize: 13
    font.weight: Font.Medium

    contentItem: Text {
        objectName: tip.objectName.length > 0 ? tip.objectName + "-text" : ""
        text: tip.text
        color: Theme.ink
        font: tip.font
        wrapMode: Text.Wrap
    }

    background: Item {
        objectName: tip.objectName.length > 0 ? tip.objectName + "-background" : ""

        Rectangle {
            x: 1
            y: 3
            width: parent.width
            height: parent.height
            radius: 9
            color: "#000000"
            opacity: Theme.dark ? 0.48 : 0.14
        }

        Rectangle {
            id: panel

            objectName: tip.objectName.length > 0 ? tip.objectName + "-panel" : ""
            anchors.fill: parent
            radius: 9
            color: Theme.surface
            border.color: Theme.rule
            border.width: 1
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 90 }
    }

    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 70 }
    }
}
