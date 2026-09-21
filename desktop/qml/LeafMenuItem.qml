// One mutually-exclusive choice in a Leaf menu. A radio mark says what this is more honestly
// than Basic's platform-coloured checkbox: the sort order can be one thing, never several.

import QtQuick
import QtQuick.Controls
import Leaf

MenuItem {
    id: option

    implicitWidth: 270
    implicitHeight: 46
    leftPadding: 14
    rightPadding: 14
    topPadding: 0
    bottomPadding: 0
    spacing: 12

    font.family: Theme.textFamily
    font.pixelSize: 15
    font.weight: checked ? Font.Medium : Font.Normal

    contentItem: Text {
        leftPadding: option.checkable && option.indicator
                     ? option.indicator.width + option.spacing : 0
        text: option.text
        color: !option.enabled ? Theme.inkFaint
                              : (option.checked ? Theme.emerald : Theme.ink)
        font: option.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Item {
        x: option.leftPadding
        y: (option.height - height) / 2
        implicitWidth: 18
        implicitHeight: 18
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
            width: 8
            height: 8
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
