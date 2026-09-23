// The heading of a block, and the line that says what is under it.
//
// The same shape as « Dans cette bibliothèque » on the description tab — small capitals, grey
// — because it does the same job: naming a block on a page that has several. The count sits
// beside the heading and not under it, where it would read as the first item of the block.

import QtQuick
import Leaf

Item {
    id: heading

    required property string title
    property string line: ""

    implicitHeight: Math.max(word.implicitHeight, said.implicitHeight)

    Text {
        id: word

        anchors.verticalCenter: parent.verticalCenter
        text: heading.title
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: 11
        font.capitalization: Font.AllUppercase
    }

    Text {
        id: said

        anchors.left: word.right
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: heading.line
        visible: text.length > 0
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: 12
        elide: Text.ElideRight
    }
}
