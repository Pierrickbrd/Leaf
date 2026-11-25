// The pages of another volume that take the place of a chapter's: the range the chapter has
// now, the one the file brings, the pages around it, and the two numbers that narrow it.
//
// No image is drawn: a page is a number, since the file is not opened by the client and the
// numbers are what a reader compares against the file's own viewer.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Column {
    id: splice

    /// `Review.sheet.splice`: `{id, name, current, source, extra, around, note}`.
    property var splice_: ({})

    objectName: "review-splice"
    visible: splice.splice_.id !== undefined
    spacing: Widths.space.row

    Row {
        spacing: Widths.space.card

        Repeater {
            model: splice.visible ? [splice.splice_.current, splice.splice_.source] : []

            delegate: Column {
                id: side

                required property var modelData
                required property int index

                spacing: Widths.space.compact

                Text {
                    textFormat: Text.PlainText
                    text: side.index === 0 ? ReviewCaptions.spliceCurrentSide()
                                           : ReviewCaptions.spliceSourceSide()
                    color: Theme.inkFaint
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                    font.weight: Font.DemiBold
                }

                Text {
                    objectName: "review-splice-" + (side.index === 0 ? "current" : "source")
                    textFormat: Text.PlainText
                    text: ReviewCaptions.pagesRange(side.modelData.from, side.modelData.to)
                    color: Theme.ink
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(13)
                }
            }
        }

        Text {
            objectName: "review-splice-extra"
            anchors.bottom: parent.bottom
            textFormat: Text.PlainText
            text: splice.splice_.extra ?? ""
            color: Theme.emerald
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
            font.weight: Font.DemiBold
        }
    }

    // The pages around the range, the ones inside it set apart.
    Row {
        objectName: "review-splice-pages"
        spacing: Widths.space.compact

        Repeater {
            model: splice.visible ? splice.splice_.around : []

            delegate: Rectangle {
                id: thumb

                required property var modelData

                width: Math.max(34, label.implicitWidth + 12)
                height: 44
                radius: 6
                color: thumb.modelData.gap ? "transparent"
                       : thumb.modelData.inside ? Theme.emerald : Theme.onBar
                border.color: Theme.rule
                border.width: thumb.modelData.gap ? 0 : 1

                Text {
                    id: label

                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: thumb.modelData.gap ? "…" : thumb.modelData.page
                    color: thumb.modelData.inside && !thumb.modelData.gap ? Theme.onEmerald
                                                                           : Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                }
            }
        }
    }

    Row {
        spacing: Widths.space.row

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: ReviewCaptions.spliceFromLabel()
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
        }

        LeafField {
            id: fromField

            objectName: "review-splice-from"
            width: 72
            activeFocusOnTab: true
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 1 }
            text: splice.visible ? String(splice.splice_.source.from) : ""
            onEditingFinished: Review.edits.setSplice(splice.splice_.id, Number(text), Number(toField.text))
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: ReviewCaptions.spliceToLabel()
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
        }

        LeafField {
            id: toField

            objectName: "review-splice-to"
            width: 72
            activeFocusOnTab: true
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 1 }
            text: splice.visible ? String(splice.splice_.source.to) : ""
            onEditingFinished: Review.edits.setSplice(splice.splice_.id, Number(fromField.text), Number(text))
        }
    }

    Text {
        objectName: "review-splice-note"
        width: splice.width
        textFormat: Text.PlainText
        text: splice.splice_.note ?? ""
        wrapMode: Text.Wrap
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
    }
}
