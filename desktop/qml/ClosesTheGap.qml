import QtQuick
import Leaf

// What the neighbours of an erased tile do once its row goes. Along the line they slide and
// settle, and only the vertical move overshoots. A cover that goes back to the end of the line
// above does not cross the grid: it crossed the whole of it over the others, and with the
// overshoot ended off the screen. It goes there at once and fades in.
//
// Its own file, and left out of the QML cache in CMakeLists.txt: Qt 6.4's cache compiler emits an
// empty function for every ScriptAction, with two parameters it never uses, and warnings are
// errors.
Transition {
    ScriptAction {
        script: {
            const item = ViewTransition.item
            if (!item || ViewTransition.destination.y === item.y)
                return
            item.x = ViewTransition.destination.x
            item.y = ViewTransition.destination.y
            item.opacity = 0
        }
    }
    ParallelAnimation {
        NumberAnimation {
            property: "x"
            duration: Preferences.motion === Preferences.Slide ? 230 : 1
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            property: "y"
            duration: Preferences.motion === Preferences.Slide ? 230 : 1
            easing.type: Easing.OutBack
            easing.overshoot: 1.3
        }
        NumberAnimation {
            property: "opacity"
            to: 1
            duration: 230
            easing.type: Easing.OutCubic
        }
    }
}
