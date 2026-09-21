// A quiet edge for scrollable paper: content fades under it instead of being guillotined.

import QtQuick
import Leaf

Rectangle {
    id: veil

    property bool leading: true
    property bool shown: false

    function paper(alpha) {
        return Qt.rgba(Theme.paper.r, Theme.paper.g, Theme.paper.b, alpha)
    }

    height: 22
    color: "transparent"
    z: 40
    opacity: shown ? 1 : 0
    visible: shown || opacity > 0

    Behavior on opacity {
        NumberAnimation {
            duration: 120
            easing.type: Easing.OutQuad
        }
    }

    gradient: Gradient {
        GradientStop {
            position: 0
            color: veil.leading ? veil.paper(0.94) : veil.paper(0)
        }
        GradientStop {
            position: 0.55
            color: veil.leading ? veil.paper(0.45) : veil.paper(0.45)
        }
        GradientStop {
            position: 1
            color: veil.leading ? veil.paper(0) : veil.paper(0.94)
        }
    }
}
