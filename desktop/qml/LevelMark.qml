// The sign of a level of the model: a universe, a work, an edition or a volume.
//
// An `Image` tinted rather than a glyph: the Material Symbols carry no `fill` of their own,
// the colour comes from the theme, and `ColorOverlay` is what gives it to them without
// touching the file. `SettingsChoice` already tints its own pill icons exactly this way —
// see the note beside its own `ColorOverlay` — because the glyphs are shipped as Google
// publishes them, unmodified.

import QtQuick
import Qt5Compat.GraphicalEffects
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

    Image {
        id: glyph

        anchors.fill: parent
        source: "assets/icons/" + CardCaptions.levelIcon(mark.level) + ".svg"
        sourceSize.width: mark.size * 2
        sourceSize.height: mark.size * 2
        fillMode: Image.PreserveAspectFit
        smooth: true
        visible: false
    }

    // The glyphs ship without a fill, so one file serves both palettes and every level —
    // see the note above.
    ColorOverlay {
        anchors.fill: glyph
        source: glyph
        color: mark.tint
    }
}
