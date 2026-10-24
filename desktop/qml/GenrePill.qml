// One genre of a series, and the way to every other series that has it.
//
// Drawn twice on a series page — in the header, where it says at a glance what kind of book
// this is, and at the foot of the description, where it closes the civil status — and
// written once here: the two were the same rectangle copied, and only one of them would
// have learned to be pressed.
//
// Pressed, it does what the universe above it does: the shelf, narrowed to that one genre.
// It answers the pointer the way a filter chip does — see `Pill` — and is never lit: it
// selects nothing where it stands.

import QtQuick
import Leaf

Pill {
    id: genre

    required property string modelData

    signal asked(string genre)

    objectName: "genre-" + modelData
    label: modelData
    textSize: 12
    padding: 11
    height: 25
    onPressed: genre.asked(genre.modelData)

    Accessible.role: Accessible.Link
    Accessible.name: modelData
    Accessible.onPressAction: genre.asked(genre.modelData)

    /// A stop of the page's walk — see `KeyWalk`.
    readonly property bool keyStop: true

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            genre.asked(genre.modelData)
            event.accepted = true
        }
    }

    FocusRing {
        objectName: genre.objectName + "-focus"
        cornerRadius: genre.height / 2
    }
}
