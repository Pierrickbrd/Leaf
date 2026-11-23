// The way back out of a page, at the head of the page's own strip.
//
// The page's, and not the application bar's: a control that leaves a page belongs to that
// page. It was put in the bar beside the name on 24/10/2026, to stand at one height on the
// series page and in the settings; there it read as part of the brand, a command of the
// application that changed meaning with every screen (owner, 25/10/2026). It is one height
// again by standing in the same place on both pages — the first thing of the strip, at the
// gutter, `top` under the bar.

import QtQuick
import Leaf

BarButton {
    objectName: "back-button"
    visible: Navigation.canGoBack
    source: "assets/icons/arrow_back.svg"
    label: Navigation.backLabel
    // As tall as the tabs it leads, and not as the bar's buttons: on a narrow window those
    // shrink with the bar, and centred on the settings' 34-pixel tabs this one stood 2 pixels
    // lower than the series page's.
    height: 34
    onTriggered: Navigation.back()
}
