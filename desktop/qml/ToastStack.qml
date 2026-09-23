// The bubbles, where the reader asked for them.
//
// Three at a time, the newest at the bottom — the order a stack that grows downwards is read
// in. Past three, one line saying how many are waiting: forty volumes that fail do not make
// forty bubbles.
//
// Laid over everything and taking no room from it: this is a layer, not a part of a screen,
// and the screen under it must not move when one appears.

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

    Column {
        id: stack

        x: layer.column === 0 ? 20
                              : (layer.column === 1 ? Math.round((layer.width - width) / 2)
                                                    : layer.width - width - 20)
        y: layer.atTheTop ? 20 : layer.height - height - 20
        width: 420
        spacing: 8

        Text {
            objectName: "toast-more"
            width: parent.width
            text: Toasts.moreLabel
            visible: Toasts.more > 0
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
        }

        Repeater {
            model: Toasts

            Toast {
                onActed: Toasts.act(index)
                onDismissed: Toasts.dismiss(index)
            }
        }
    }
}
