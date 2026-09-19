// A state, said with a picto rather than spelled out in facts.
//
// What a reader needs of a server is whether their books are there — not its address, not
// its version, not where the key sits. Those appear under this line and only when something
// is wrong, which is the one moment they help rather than clutter.

import QtQuick
import Qt5Compat.GraphicalEffects
import Leaf

Item {
    id: state

    required property string icon
    required property string label
    property string detail
    /// Drawn in the alert colour, for a state that is a problem rather than a fact.
    property bool alarming: false

    implicitHeight: Math.max(36, words.height)
    height: implicitHeight

    Image {
        id: glyph

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 2
        width: 22
        height: 22
        source: "assets/icons/" + state.icon + ".svg"
        sourceSize: Qt.size(22, 22)
        visible: false
    }

    ColorOverlay {
        anchors.fill: glyph
        source: glyph
        color: state.alarming ? Theme.alert : Theme.emerald
    }

    Column {
        id: words

        anchors.left: glyph.right
        anchors.leftMargin: 12
        anchors.right: parent.right
        spacing: 2

        Text {
            objectName: state.objectName + "-label"
            width: parent.width
            text: state.label
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: 15
            font.weight: Font.Medium
            wrapMode: Text.Wrap
        }

        Text {
            objectName: state.objectName + "-detail"
            width: parent.width
            visible: state.detail.length > 0
            text: state.detail
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 13
            lineHeight: 1.3
            wrapMode: Text.Wrap
        }
    }
}
