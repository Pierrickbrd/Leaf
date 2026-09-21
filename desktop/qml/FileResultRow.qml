// A file hit: what matched first, then enough context to know what opening it means.

import QtQuick
import Leaf

Rectangle {
    id: row

    required property int index
    required property string kind
    required property string resultId
    required property string label
    required property string seriesId
    required property string seriesName
    required property string entryId
    required property string cover
    required property string context
    required property bool selected

    signal pointerEntered()
    signal pointerExited()
    signal activated()

    readonly property bool hovered: pointer.hovered

    objectName: "search-result-" + resultId
    height: 72
    radius: Theme.buttonRadius
    color: selected || hovered ? Theme.onPaper : "transparent"

    Accessible.role: Accessible.ListItem
    Accessible.name: label
    Accessible.description: context
    Accessible.focusable: true
    Accessible.focused: selected
    Accessible.onPressAction: row.activated()

    HoverHandler {
        id: pointer
        cursorShape: Qt.PointingHandCursor
        onHoveredChanged: hovered ? row.pointerEntered() : row.pointerExited()
    }

    TapHandler {
        onTapped: row.activated()
    }

    Item {
        id: coverFrame

        x: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 36
        height: 54

        Rectangle {
            anchors.fill: parent
            radius: 6
            color: Theme.onPaper
            border.color: Theme.rule
            border.width: 1
            clip: true
            antialiasing: true

            Image {
                anchors.fill: parent
                source: row.cover
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
            }
        }
    }

    Text {
        id: resultName

        anchors.left: coverFrame.right
        anchors.leftMargin: 12
        anchors.right: arrow.left
        anchors.rightMargin: 12
        anchors.top: parent.top
        anchors.topMargin: 13
        text: row.label
        color: Theme.ink
        font.family: Theme.displayFamily
        font.pixelSize: 16
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }

    Text {
        objectName: "search-context-" + row.resultId
        anchors.left: resultName.left
        anchors.right: resultName.right
        anchors.top: resultName.bottom
        anchors.topMargin: 5
        text: row.context
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: 13
        elide: Text.ElideRight
    }

    Text {
        id: arrow

        anchors.right: parent.right
        anchors.rightMargin: 13
        anchors.verticalCenter: parent.verticalCenter
        text: "›"
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: 23
    }

    // An entry may keep a visible selection after a click, but its outline stays inside the
    // row. The generic ring deliberately grows outside its owner; inside a clipped ListView
    // that left only two long horizontal strokes and cut off both sides.
    Rectangle {
        anchors.fill: parent
        radius: row.radius
        color: "transparent"
        border.color: Theme.emerald
        border.width: 2
        visible: row.selected
        antialiasing: true
    }
}
