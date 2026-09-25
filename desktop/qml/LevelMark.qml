// The sign of a level of the model: a universe, a work, an edition or a volume.
//
// A `Glyph`, which is how every tinted symbol in this client is drawn: the Material Symbols
// carry no `fill` of their own and the colour comes from the theme. This one adds what the
// others do not need — a name to be read aloud, and the level-to-symbol lookup.
//
// It used to write the `Image` and its `ColorOverlay` out for itself, and said so in a
// comment pointing at `SettingsChoice`, which wrote the same six lines. Eleven places did.

import QtQuick
import Leaf

Item {
    id: mark

    required property int level
    property int size: 16
    property color tint: Theme.inkSoft

    // Named from outside, by the line that draws it — never here. `ImportNode` names its
    // own three `Text` children the same way (`line.objectName + "-…"`); a mark left to
    // name itself answered the same "level-mark" for every card, so a search caught
    // whichever one it asked first rather than the one it meant.
    implicitWidth: size
    implicitHeight: size
    width: implicitWidth
    height: implicitHeight

    Accessible.role: Accessible.Graphic
    Accessible.name: CardCaptions.levelLabel(mark.level)

    Glyph {
        anchors.fill: parent
        side: mark.size
        // Twice what it is drawn at: this is the one glyph here that is scaled rather than
        // placed, and an SVG rasterised at its own size then scaled is an SVG drawn soft.
        resolution: mark.size * 2
        source: "assets/icons/" + CardCaptions.levelIcon(mark.level) + ".svg"
        tint: mark.tint
    }
}
