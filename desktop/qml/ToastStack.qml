// The bubbles, where the reader asked for them.
//
// Three at a time, the newest at the bottom — the order a stack that grows downwards is read
// in. Past three, one line saying how many are waiting: forty volumes that fail do not make
// forty bubbles. Two on a short window (`Widths.short`): on 23/10/2026, at 800 × 560, three
// bubbles and their count stood from the foot of the window to the bar.
//
// Laid over everything and taking no room from it: this is a layer, not a part of a screen,
// and the screen under it must not move when one appears.
//
// At the page's gutter from the window's edge, and as wide as its words. On 23/10/2026 it
// stood 20 from the edge where every page stands at the gutter, 420 wide at any window: at
// 800 × 560 three bubbles covered the right half of the shelf, and at 1920 they were small
// in the corner. `column(420)` grows them as a card of words grows, and never past the
// window less its two gutters.

import QtQuick
import Leaf

Item {
    id: layer

    /// The six zones, as `Preferences.Corner` numbers them.
    readonly property int corner: Toasts.corner
    readonly property bool atTheTop: corner <= 2
    readonly property int column: corner % 3

    objectName: "toast-stack"
    anchors.fill: parent
    // A layer with nothing in it takes no clicks: the shelf underneath is still a shelf.
    visible: Toasts.count > 0 || Toasts.more > 0
    z: 50

    Binding {
        target: Toasts
        property: "cramped"
        value: Widths.short
    }

    Column {
        id: stack

        readonly property int edge: Widths.space.gutter

        objectName: "toast-column"
        x: layer.column === 0 ? edge
                              : (layer.column === 1 ? Math.round((layer.width - width) / 2)
                                                    : layer.width - width - edge)
        y: layer.atTheTop ? edge : layer.height - height - edge
        width: Math.max(0, Math.min(Widths.scale.column(420), layer.width - 2 * edge))
        spacing: Widths.space.compact

        Text {
            objectName: "toast-more"
            width: parent.width
            textFormat: Text.PlainText
            text: Toasts.moreLabel
            visible: Toasts.more > 0
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            horizontalAlignment: Text.AlignRight
        }

        Repeater {
            model: Toasts

            Toast {
                onActed: Toasts.act(index)
                onDismissed: Toasts.dismiss(index)
                onHeld: on => Toasts.hold(index, on)
            }
        }
    }
}
