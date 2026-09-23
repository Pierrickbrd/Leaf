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
    implicitHeight: 40

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

                objectName: "series-tab-" + index
                width: word.implicitWidth + 28
                height: 38

                TapHandler { onTapped: bar.chosen(parent.index) }
                HoverHandler { cursorShape: Qt.PointingHandCursor }

                Text {
                    id: word

                    anchors.centerIn: parent
                    text: parent.modelData
                    color: bar.current === parent.index ? Theme.ink : Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 15
                    font.weight: bar.current === parent.index ? Font.DemiBold : Font.Normal
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 2
                    color: Theme.emerald
                    visible: bar.current === parent.index
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
        anchors.bottomMargin: 4
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
