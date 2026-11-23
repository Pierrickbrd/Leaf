// What the shelf says when it has no covers to show: a mark, a headline and a sentence.
//
// One component for the three ways a shelf can be bare — a library with nothing in it, a
// filter that left nothing, a first page that failed — because they are the same thing to
// the person looking at them: the one sentence on the screen. In the audit of 23/10/2026 the
// empty library drew nothing at all, four hundred and fifty pixels of paper under the pills,
// and the failure drew a card of its own with its own sizes; two answers to one question.
//
// The headline is one of the three words the owner allowed to grow with the window
// (`Widths.scale.display`): it is the subject of the whole screen. Everything else follows
// the window's rules for words, and the box takes its height from them.
//
// `framed` is the same words in a card, for a trouble that arrives while covers are already
// drawn: laid over them at the bottom, on paper of its own, and without a headline — the
// shelf is still there, so nothing is lost but the next page.

import QtQuick
import Leaf

Rectangle {
    id: notice

    /// A symbol from `assets/icons`, by name; none when empty.
    property string glyph: ""
    property string headline: ""
    property string said: ""
    property bool framed: false

    readonly property int padding: framed ? Widths.scale.size(16) : 0

    implicitWidth: Widths.scale.column(440)
    implicitHeight: words.implicitHeight + 2 * padding
    radius: Theme.cardRadius
    color: framed ? Theme.surface : "transparent"

    Accessible.role: Accessible.StaticText
    Accessible.name: notice.headline.length > 0 ? notice.headline : notice.said
    Accessible.description: notice.headline.length > 0 ? notice.said : ""

    Column {
        id: words

        x: notice.padding
        y: notice.padding
        width: notice.width - 2 * notice.padding
        spacing: Widths.scale.room(10)

        Glyph {
            objectName: notice.objectName + "-glyph"
            anchors.horizontalCenter: parent.horizontalCenter
            side: Widths.scale.icon(36)
            source: notice.glyph.length > 0 ? "assets/icons/" + notice.glyph + ".svg" : ""
            tint: Theme.inkFaint
            visible: notice.glyph.length > 0 && !notice.framed
        }

        Text {
            objectName: notice.objectName + "-headline"
            width: parent.width
            textFormat: Text.PlainText
            text: notice.headline
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: Widths.scale.display(28)
            font.weight: Font.Bold
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            visible: text.length > 0 && !notice.framed
        }

        Text {
            objectName: notice.objectName + "-text"
            width: parent.width
            textFormat: Text.PlainText
            text: notice.said
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(15)
            lineHeightMode: Text.FixedHeight
            lineHeight: Math.round(font.pixelSize * 1.45)
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            visible: text.length > 0
        }
    }
}
