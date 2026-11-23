// The three tabs, and the commands of the list at the other end of the bar.
//
// Liste / grille and the magnifier sit there and nowhere else: they are commands *of the
// list*, the way Filtrer and Trier are commands of the shelf. Neither exists on the two other
// tabs, because there is nothing there for them to act on.

import QtQuick
import Leaf

Item {
    id: bar

    required property int current
    required property bool asGrid
    required property string query
    /// Whether the last tab has anything to show. Drawn or not at all: a tab that opens on
    /// an empty page is worse than one tab fewer.
    required property bool hasElsewhere

    signal chosen(int which)
    signal viewToggled()
    signal searched(string text)

    objectName: "series-tabs"
    /// As tall as the commands of the list centred on the tabs' words, with `compact` between
    /// them and the rule: on tabs of 38 the volume search's frame stood 3 pixels above it,
    /// closer to the rule than to anything it belonged to (audit, 25/10/2026).
    readonly property int tabHeight: 2 * (34 / 2 + Widths.space.compact + 1)

    implicitHeight: tabHeight + 2

    Row {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        spacing: 2

        Repeater {
            model: bar.hasElsewhere
                   ? [SeriesCaptions.volumesTab, SeriesCaptions.descriptionTab,
                      SeriesCaptions.elsewhereTab]
                   : [SeriesCaptions.volumesTab, SeriesCaptions.descriptionTab]

            Item {
                required property int index
                required property string modelData

                id: tab

                objectName: "series-tab-" + index
                width: word.implicitWidth + 28
                height: bar.tabHeight

                /// A stop of the page's walk — see `KeyWalk`.
                readonly property bool keyStop: true

                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                            || event.key === Qt.Key_Enter) {
                        bar.chosen(tab.index)
                        event.accepted = true
                    }
                }

                /// Lit by the keys alone, as every ring — see `FocusRing`.
                readonly property var cursor: Window.window ? Window.window.navigationCursor
                                                            : null
                readonly property bool keyed: tab.activeFocus
                                              && !(cursor && cursor.pointerTookOver)

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: bar.chosen(parent.index)
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }

                Text {
                    id: word

                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: parent.modelData
                    color: bar.current === parent.index ? Theme.ink : Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 15
                    font.weight: bar.current === parent.index ? Font.DemiBold : Font.Normal
                }

                // The underline that says which tab is open is also the keys' focus: emerald and
                // thicker on the open tab, a quiet one under another. A ring round a tab was
                // a frame as tall as the bar round a single word.
                Rectangle {
                    objectName: tab.objectName + "-focus"
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: tab.keyed && bar.current === tab.index ? 3 : 2
                    color: bar.current === tab.index ? Theme.emerald : Theme.inkSoft
                    visible: bar.current === tab.index || tab.keyed
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.rule
        z: -1
    }

    // Only on the volumes tab: a command of a list needs a list.
    Row {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: bar.tabHeight / 2 - 34 / 2
        spacing: 6
        visible: bar.current === 0

        LeafSearchLine {
            objectName: "volume-search"
            placeholder: SeriesCaptions.volumesAxis
            text: bar.query
            width: 220
            anchors.verticalCenter: parent.verticalCenter
            onAsked: text => bar.searched(text)
        }

        // One target, wearing the icon of the mode it switches *to*. Two segments side by
        // side ask a reader to work out which of them is lit before aiming at the other;
        // a button that changes says where it goes.
        IconButton {
            objectName: "volume-view"
            anchors.verticalCenter: parent.verticalCenter
            width: 34
            height: 34
            side: 20
            glyph: bar.asGrid ? "view_list" : "grid_view"
            label: bar.asGrid ? SeriesCaptions.asList : SeriesCaptions.asGrid
            onTriggered: bar.viewToggled()
        }
    }

}
