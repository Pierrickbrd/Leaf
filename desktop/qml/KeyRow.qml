// One line of keys: the keys, the signs between them, and a mouse gesture or a hint beside.
//
// Cut out of the reader's shortcuts card when the settings screen wanted the same keys in a
// smaller size. Each element is `{kind, text, glyph, wide}` as `KeyShapes.h` makes them.
//
// No `pragma ComponentBehavior: Bound` here, for the reason `ReaderShortcuts` gives: the four
// shapes are Components made by a Loader, which Qt 6.4 (Ubuntu 24.04) refuses in a bound file.

import QtQuick
import Leaf

Row {
    id: row

    property var keys: []
    property bool compact: false

    spacing: Widths.scale.size(row.compact ? 5 : 7)

    Repeater {
        model: row.keys

        Item {
            id: one

            required property var modelData

            width: drawn.item ? drawn.item.width : 0
            height: Widths.scale.icon(row.compact ? 26 : 34)

            Loader {
                id: drawn

                anchors.verticalCenter: parent.verticalCenter
                sourceComponent: one.modelData.kind === "key" ? aKey
                               : one.modelData.kind === "gesture" ? aGesture
                               : one.modelData.kind === "hint" ? aHint
                               : aSign
            }

            Component {
                id: aKey

                Keycap {
                    compact: row.compact
                    text: one.modelData.text || ""
                    glyph: one.modelData.glyph || ""
                    wide: !!one.modelData.wide
                }
            }

            Component {
                id: aGesture

                Item {
                    width: Widths.scale.icon(row.compact ? 26 : 34)
                    height: width

                    Glyph {
                        anchors.centerIn: parent
                        source: "assets/icons/" + one.modelData.glyph + ".svg"
                        side: Widths.scale.icon(row.compact ? 24 : 32)
                        tint: Theme.emerald
                    }
                }
            }

            Component {
                id: aHint

                Text {
                    leftPadding: Widths.scale.size(4)
                    text: one.modelData.text
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(row.compact ? 11 : 12)
                }
            }

            Component {
                id: aSign

                Text {
                    leftPadding: Widths.scale.size(2)
                    rightPadding: Widths.scale.size(2)
                    text: one.modelData.text
                    color: Theme.inkFaint
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(row.compact ? 11 : 12)
                }
            }
        }
    }
}
