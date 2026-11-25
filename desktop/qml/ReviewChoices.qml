// Where a file that is another volume than the one aimed at goes: its own place, first and
// recommended, or the aimed volume replaced all the same. One is always chosen; choosing the
// other is a correction of the file's number, like any other.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Column {
    id: choices

    /// `Review.sheet.choices`: `[{id, label, badge, detail, chosen}]`.
    property var choices_: []
    /// The id of the line they are about.
    property string about: ""

    objectName: "review-choices"
    visible: choices.choices_.length > 0
    spacing: Widths.space.compact

    Repeater {
        model: choices.choices_

        delegate: ReviewChoice {
            id: one

            required property var modelData

            objectName: "review-choice-" + one.modelData.id
            width: choices.width
            label: one.modelData.label
            detail: one.modelData.detail
            badge: one.modelData.badge
            chosen: one.modelData.chosen === true
            onPicked: Review.edits.choose(choices.about, one.modelData.id)
        }
    }
}
