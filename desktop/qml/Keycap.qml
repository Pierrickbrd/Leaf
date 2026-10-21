// One key of the reader's shortcuts card: an emerald face on a deeper lip.
//
// The lip is what makes a pill a key. Six pixels of `Theme.emeraldLip` showing under a face of
// twenty-eight, as the mockup approved on 22/10/2026 draws it with an inset shadow; the soft
// shadow under the whole key is `CardLift`'s, the one every lifted thing in this client has,
// and never a second darker rectangle laid under the first.
//
// What it says is a word (`text`) in Inter Medium, or a Tabler glyph (`glyph`), in the tone
// written on emerald. `wide` pads a word longer than a letter, and the backspace glyph.
//
// It follows the window as an icon does (`Widths.scale.icon`), at half a length's rate above
// 1280: a key of its drawn size on a grown card read as a speck (owner's test of 22/10/2026),
// and one grown at the full rate stood larger than the words beside it once those stopped
// growing (23/10/2026).

import QtQuick
import Leaf

Rectangle {
    id: cap

    property string text: ""
    property string glyph: ""
    property bool wide: false
    readonly property int face: Widths.scale.icon(28)
    readonly property int lip: Widths.scale.icon(6)
    readonly property real content: cap.glyph.length > 0 ? mark.width : word.implicitWidth

    implicitWidth: Math.max(Widths.scale.icon(32), Math.ceil(cap.content))
                   + (cap.wide ? Widths.scale.icon(22) : 0)
    implicitHeight: cap.face + cap.lip
    width: implicitWidth
    height: implicitHeight
    radius: Widths.scale.icon(7)
    color: Theme.emeraldLip
    antialiasing: true

    CardLift {
        level: CardLift.Card
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: cap.face
        radius: cap.radius
        color: Theme.emerald
        antialiasing: true

        Text {
            id: word

            objectName: "keycap-text"
            anchors.centerIn: parent
            visible: cap.glyph.length === 0
            text: cap.text
            color: Theme.onEmerald
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(13)
            font.weight: Font.Medium
        }

        Glyph {
            id: mark

            anchors.centerIn: parent
            visible: cap.glyph.length > 0
            source: cap.glyph.length > 0 ? "assets/icons/" + cap.glyph + ".svg" : ""
            side: Widths.scale.icon(cap.wide ? 20 : 18)
            tint: Theme.onEmerald
        }
    }
}
