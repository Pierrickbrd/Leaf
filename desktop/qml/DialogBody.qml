// The inside of a modal window, and what the keys do there that the window does not.
//
// The settings promise that the arrows go from one control to the next, as Tab does. On the
// page that is `Main`'s doing; a window opened over it lives in the overlay, outside the page,
// and the arrows did nothing there at all. A key no control took comes up to here and walks
// the window's own chain — which ends at the window, as Tab's does.
//
// The focus goes back where it came from when the window closes: Qt hands it to the page,
// and the next Tab started from the top of the screen instead of from the pencil pressed.
//
// And « / », which the bar takes for the search from anywhere in the application: pressed on
// a button of a modal window it put the focus in the search field behind it, where the window
// could no longer hear Escape, and the person was left in front of a window that would not
// close.

import QtQuick
import QtQuick.Controls

Item {
    id: body

    /// What had the focus when the window opened.
    property Item opener: null
    /// What the window opens on, at every opening. A `focus: true` says it only the first
    /// time: a window is a scope, and opened again it gave the keys back to whatever held them
    /// when it closed — the red button of a deletion pressed once was where the next deletion
    /// opened, and a habitual Enter deleted (audit, 25/10/2026).
    property Item initialFocus: null

    function settle() {
        if (body.visible && body.initialFocus && body.initialFocus.visible)
            body.initialFocus.forceActiveFocus(Qt.PopupFocusReason)
    }

    // Shown with the window and before it takes the focus; hidden once Qt has given it away —
    // to the page, or to another window still open over it, which keeps it: the recovery code
    // comes up while the profile window closes, and lost its keys to the pencil behind both.
    onVisibleChanged: {
        if (visible) {
            body.opener = body.Window.activeFocusItem
            // Once the window has handed out its own focus, so that this one is the last word.
            Qt.callLater(body.settle)
            return
        }
        // Asked of the opener: a closed window has left the window it was drawn in.
        const opener = body.opener
        body.opener = null
        if (!opener || !opener.visible)
            return
        for (let at = opener.Window.activeFocusItem; at; at = at.parent) {
            if (at === opener.Overlay.overlay)
                return
        }
        opener.forceActiveFocus(Qt.PopupFocusReason)
    }

    Keys.onShortcutOverride: event => {
        if (event.text === "/")
            event.accepted = true
    }

    Keys.onPressed: event => {
        const forward = event.key === Qt.Key_Down || event.key === Qt.Key_Right
        if (!forward && event.key !== Qt.Key_Up && event.key !== Qt.Key_Left)
            return
        const from = body.Window.activeFocusItem
        // Left and Right belong to a field, even at its ends: they move its cursor.
        if (!from || (from.cursorPosition !== undefined
                      && (event.key === Qt.Key_Left || event.key === Qt.Key_Right)))
            return
        const next = from.nextItemInFocusChain(forward)
        if (next && next !== from)
            next.forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
        event.accepted = true
    }
}
