// Two columns beside each other: what the library holds of a volume, and what the file
// brings. A value that differs is set in the emerald, so the eye goes to what a replacement
// would change and not to the many things it leaves as they are.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Row {
    id: cmp

    /// `{left: {title, rows}, right: {title, rows}}` — `Review.sheet.compare`.
    property var compare: ({})

    objectName: "review-compare"
    visible: cmp.compare.left !== undefined
    spacing: Widths.space.card

    Repeater {
        model: cmp.visible ? [cmp.compare.left, cmp.compare.right] : []

        delegate: Column {
            id: side

            required property var modelData
            required property int index

            width: (cmp.width - cmp.spacing) / 2
            spacing: Widths.space.compact

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: side.modelData.title
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Repeater {
                model: side.modelData.rows

                delegate: Row {
                    id: line

                    required property var modelData

                    width: side.width
                    spacing: Widths.space.compact

                    Text {
                        width: Math.min(implicitWidth, parent.width / 2)
                        textFormat: Text.PlainText
                        text: line.modelData.label
                        color: Theme.inkSoft
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(13)
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width - x
                        textFormat: Text.PlainText
                        text: line.modelData.value
                        color: line.modelData.differs ? Theme.emerald : Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(13)
                        font.weight: line.modelData.differs ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
