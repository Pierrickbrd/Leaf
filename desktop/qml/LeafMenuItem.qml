// One mutually-exclusive choice in a Leaf menu. A radio mark says what this is more honestly
// than Basic's platform-coloured checkbox: the sort order can be one thing, never several.

import QtQuick
import QtQuick.Controls
import Leaf

MenuItem {
    id: option

    // A row of words: `column()` wide, as tall as its words with air, its gaps `label`
    // from the mark to the word. At a literal 46 a desktop text half as large again filled
    // the row edge to edge.
    //
    // And wider when its words are: a profile's name, typed on another device, was cut at the
    // floor in a menu with room beside it.
    implicitWidth: Math.max(Widths.scale.column(270),
                            leftPadding + contentItem.implicitWidth + rightPadding)
    implicitHeight: Math.max(Widths.scale.size(46),
                             contentItem.implicitHeight + 2 * Widths.space.row)
    leftPadding: Widths.scale.size(14)
    rightPadding: Widths.scale.size(14)
    topPadding: 0
    bottomPadding: 0
    spacing: Widths.space.label

    font.family: Theme.textFamily
    font.pixelSize: Widths.scale.body(15)
    font.weight: checked ? Font.Medium : Font.Normal

    contentItem: Text {
        leftPadding: option.checkable && option.indicator
                     ? option.indicator.width + option.spacing : 0
        text: option.text
        // Plain, never rich: a name typed on another device — « <b>x</b> » — is shown as
        // typed, not drawn as markup.
        textFormat: Text.PlainText
        color: !option.enabled ? Theme.inkFaint
                              : (option.checked ? Theme.emerald : Theme.ink)
        font: option.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Item {
        x: option.leftPadding
        y: (option.height - height) / 2
        implicitWidth: Widths.scale.icon(18)
        implicitHeight: implicitWidth
        width: implicitWidth
        height: implicitHeight
        visible: option.checkable

        Rectangle {
            objectName: option.objectName.length > 0
                        ? option.objectName + "-indicator" : ""
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.color: option.checked ? Theme.emerald : Theme.rule
            border.width: option.checked ? 2 : 1
        }

        Rectangle {
            objectName: option.objectName.length > 0
                        ? option.objectName + "-dot" : ""
            anchors.centerIn: parent
            width: Widths.scale.icon(8)
            height: width
            radius: width / 2
            visible: option.checked
            color: Theme.emerald
        }
    }

    background: Rectangle {
        x: 2
        y: 2
        width: option.width - 4
        height: option.height - 4
        radius: 10
        color: option.down || option.highlighted
               ? Theme.onPaper
               : (option.checked ? Theme.emeraldWash : "transparent")
        border.color: option.checked ? Theme.emerald : "transparent"
        border.width: option.checked ? 1 : 0

        Behavior on color {
            ColorAnimation { duration: 90 }
        }
    }
}
