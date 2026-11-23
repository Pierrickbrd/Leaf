// One value of the library's declaration beside the one the drop brings, for each key that
// differs: the library's marked « Garder », the drop's « Nouveau », and clicking one chooses.
// « Non renseigné » says the library holds nothing there, which keeping would keep.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Column {
    id: keep

    /// `Review.sheet.fields` of the series aimed at: `[{id, key, label, present, arriving,
    /// keep}]`.
    property var pairs: []

    objectName: "review-keeps"
    visible: keep.pairs.length > 0
    spacing: Widths.space.row

    Repeater {
        model: keep.pairs

        delegate: Column {
            id: pair

            required property var modelData

            width: keep.width
            spacing: Widths.space.compact

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: pair.modelData.label
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                font.weight: Font.DemiBold
            }

            ReviewChoice {
                objectName: "review-keep-" + pair.modelData.key
                width: parent.width
                label: pair.modelData.present
                badge: ReviewCaptions.keepLabel()
                chosen: pair.modelData.keep === true
                onPicked: Review.edits.setKeep(pair.modelData.id, pair.modelData.key, true)
            }

            ReviewChoice {
                objectName: "review-keep-" + pair.modelData.key + "-new"
                width: parent.width
                label: pair.modelData.arriving
                badge: ReviewCaptions.newValueLabel()
                chosen: pair.modelData.keep !== true
                onPicked: Review.edits.setKeep(pair.modelData.id, pair.modelData.key, false)
            }
        }
    }
}
