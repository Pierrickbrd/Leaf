// A field for searching inside one long axis.
//
// Not the application's search field and deliberately not shaped like it: this narrows a list
// already on screen, where that one asks the server a question. A reader who cannot tell them
// apart will type a series name in here and conclude the library is empty.
//
// The shape is `LeafField`'s, because the field a deletion asks a name into is the same
// object with nothing to search.

import QtQuick
import Leaf

LeafField {
    id: line

    required property string placeholder

    signal asked(string text)

    placeholderText: Captions.searchWithin(placeholder)

    onTextEdited: line.asked(text)
}
