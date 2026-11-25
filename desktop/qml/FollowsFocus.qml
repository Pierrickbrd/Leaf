import QtQuick

// Scrolls its Flickable so that whatever holds the keys inside it is in sight. A Flickable
// does nothing of the kind by itself: in a window shorter than the page, Tab went on into
// controls below the edge and the ring was drawn where nobody could see it.
Item {
    id: follower

    required property Flickable flick
    /// Left around the item, for the ring that grows outside it.
    property real room: 12

    visible: false

    Connections {
        target: follower.flick.Window.window

        function onActiveFocusItemChanged() {
            const held = follower.flick.Window.window.activeFocusItem
            let inside = false
            for (let one = held; one; one = one.parent) {
                if (one === follower.flick.contentItem) {
                    inside = true
                    break
                }
            }
            if (!inside)
                return
            const top = held.mapToItem(follower.flick.contentItem, 0, 0).y
            const flick = follower.flick
            if (top - follower.room < flick.contentY)
                flick.contentY = Math.max(0, top - follower.room)
            else if (top + held.height + follower.room > flick.contentY + flick.height)
                flick.contentY = Math.min(Math.max(0, flick.contentHeight - flick.height),
                                          top + held.height + follower.room - flick.height)
        }
    }
}
