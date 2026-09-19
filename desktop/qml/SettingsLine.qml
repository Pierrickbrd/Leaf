// A fact and, sometimes, what it amounts to.
//
// Two columns and not a label with a colon: the left edge of every value lines up, which is
// what lets an eye run down the right-hand side and find the one line that is wrong.

import QtQuick
import Leaf

Item {
    id: line

    required property string label
    property string value
    /// Drawn quieter, for a fact that is an absence: no shared folder, no key found.
    property bool muted: false
    /// The one line of a card that answers the question the card asks. The rest are detail,
    /// and a card where every line shouts is a card where nothing does.
    property bool strong: false

    // See `SettingsNote`: the height is the children's, and a `Column` skips what is not
    // visible. Binding it back through `visible` is the loop that note describes.
    implicitHeight: Math.max(left.implicitHeight, right.implicitHeight)
    height: implicitHeight

    Text {
        id: left

        objectName: line.objectName.length > 0 ? line.objectName + "-label" : ""
        anchors.left: parent.left
        width: parent.width * 0.62
        text: line.label
        color: line.muted ? Theme.inkFaint : Theme.ink
        font.family: Theme.textFamily
        font.pixelSize: 14
        font.weight: line.strong ? Font.Medium : Font.Normal
        wrapMode: Text.Wrap
    }

    Text {
        id: right

        objectName: line.objectName.length > 0 ? line.objectName + "-value" : ""
        anchors.right: parent.right
        anchors.left: left.right
        anchors.leftMargin: 12
        text: line.value
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: 14
        horizontalAlignment: Text.AlignRight
        wrapMode: Text.Wrap
    }
}
