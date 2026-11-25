// A word that is not a value anyone said, as a small rounded label edged in dashes: « Calculé ·
// lecture seule » over the credits a series computes from its volumes.
//
// Dashed, and not the plain emerald outline of `OriginChip`'s « Repris »: that one says where
// a value was read, this one that it is nobody's to type — a solid edge beside the field read
// as one more origin among the four. The edge is `DashedEdge`'s.

import QtQuick
import Leaf

Item {
    id: chip

    required property string label

    implicitWidth: word.implicitWidth + 2 * Widths.scale.fit(9)
    implicitHeight: Math.max(Widths.scale.fit(22), word.implicitHeight + Widths.scale.fit(6))
    width: implicitWidth
    height: implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: chip.label

    DashedEdge {
        anchors.fill: parent
    }

    Text {
        id: word

        objectName: chip.objectName + "-text"
        anchors.centerIn: parent
        text: chip.label
        textFormat: Text.PlainText
        color: Theme.emerald
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
    }
}
