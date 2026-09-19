// A sentence the server or the setup wrote, shown as it stands.
//
// Never reworded here: `Settings.missing` names the file it wants and the variables that
// would do instead, and a scan report is the scanner's own prose. Both are worth more
// verbatim than summarised, and summarising the second would be inventing a second scanner.

import QtQuick
import Leaf

Text {
    id: note

    /// Drawn in the alert colour, for what is wrong rather than what merely is.
    property bool alarming: false
    /// Quieter still, for what closes a list rather than belonging to it.
    property bool muted: false

    width: parent ? parent.width : 0
    // No height of its own. A wrapped `Text` whose height is bound to its own
    // `implicitHeight` is a binding loop Qt reports four times per layout, and a `Column`
    // already skips a child that is not visible — so the binding buys nothing and costs
    // that. Fixed once in `SearchWorkspace` and written again here a day later, which is
    // why the note is now beside the pattern rather than beside one instance of it.
    color: alarming ? Theme.alert : (muted ? Theme.inkFaint : Theme.inkSoft)
    font.family: Theme.textFamily
    font.pixelSize: 13
    lineHeight: 1.35
    wrapMode: Text.Wrap
    topPadding: 2
}
