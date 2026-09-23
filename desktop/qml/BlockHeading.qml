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
        font.pixelSize: 12
        font.capitalization: Font.AllUppercase
    }

    // At the far end of the block and not against the heading: the two are a pair holding
    // the width of what is under them — « Dans l'univers … 3 autres » reads as one line about
    // the block, where the count tucked against the title read as part of the title.
    Text {
        id: said

        anchors.left: word.right
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignRight
        text: heading.line
        visible: text.length > 0
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: 13
        elide: Text.ElideRight
    }
}
