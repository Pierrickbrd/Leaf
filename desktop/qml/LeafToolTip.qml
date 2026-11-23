// The application's one tooltip: the same warm surface, type and restrained elevation as
// everything it points at. Qt's Basic tooltip is deliberately neutral; leaving it in place
// makes the first hover introduce a third palette into the window.

import QtQuick
import QtQuick.Controls
import Leaf

ToolTip {
    id: tip

    /// Plain by default: a hint can carry text the library holds, and a title spelled
    /// « <img src=… > » would be read as markup, and its address fetched. A hint that is
    /// markup on purpose says so.
    property int textFormat: Text.PlainText

    // `compact` from what it points at, inside and to the window's edge, as every popup's;
    // its sides a little wider than that, the words of one line wanting air either side.
    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: parent ? parent.height + Widths.space.compact : 0

    margins: Widths.space.compact
    // The air at the sides is the content's own, not the popup's `leftPadding`: Qt 6.4 reports a
    // binding loop on `implicitWidth` when a popup's side paddings follow the text size and the
    // window is resized under it, which 6.10 lets pass.
    leftPadding: 0
    rightPadding: 0
    topPadding: Widths.space.compact
    bottomPadding: Widths.space.compact
    delay: 450
    timeout: 4000

    font.family: Theme.textFamily
    font.pixelSize: Widths.scale.body(13)
    font.weight: Font.Medium

    contentItem: Item {
        implicitWidth: label.implicitWidth + 2 * Widths.scale.size(12)
        implicitHeight: label.implicitHeight

        Text {
            id: label

            objectName: tip.objectName.length > 0 ? tip.objectName + "-text" : ""
            x: Widths.scale.size(12)
            width: parent.width - 2 * Widths.scale.size(12)
            text: tip.text
            textFormat: tip.textFormat
            color: Theme.ink
            font: tip.font
            wrapMode: Text.Wrap
        }
    }

    background: Item {
        objectName: tip.objectName.length > 0 ? tip.objectName + "-background" : ""

        Rectangle {
            id: panel

            objectName: tip.objectName.length > 0 ? tip.objectName + "-panel" : ""
            anchors.fill: parent
            radius: 9
            color: Theme.surface
            border.color: Theme.rule
            border.width: 1

            CardLift { level: CardLift.Bubble }
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 90 }
    }

    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 70 }
    }
}
