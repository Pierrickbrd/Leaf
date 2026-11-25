// Where an archive set at a universe's root goes, on its sheet: declared a book, with the
// `oneshot.json` that makes it one written into it, or filed as a volume of a series — greyed,
// with its reason, since filing it there means moving the file.
//
// Without a role, the scan reads nothing at a universe's root but books: an archive left there
// would sit on the library's disk in `disregarded`, shown nowhere. This is the one place it is
// given a role from the sheet itself, beside the sentence that says it would not be read.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Column {
    id: block

    /// `Review.sheet`.
    required property var sheet
    /// The frame is a look at what already went: the place is shown, not chosen.
    property bool readOnly: false

    readonly property var places: block.sheet.place ?? []

    objectName: "review-place"
    visible: block.places.length > 0
    spacing: Widths.space.compact

    Repeater {
        model: block.places.length

        delegate: ReviewChoice {
            required property int index
            // As `ReviewRail`'s node: read once more past the end of a list that shrank.
            readonly property var place: block.places[index] ?? ({ role: -1, label: "",
                                                                   detail: "", chosen: false,
                                                                   possible: false })

            objectName: "review-place-" + index
            width: block.width
            label: place.label
            detail: place.detail
            chosen: place.chosen === true
            possible: place.possible === true
            readOnly: block.readOnly
            onPicked: Review.edits.setRole(block.sheet.id, place.role)
        }
    }
}
