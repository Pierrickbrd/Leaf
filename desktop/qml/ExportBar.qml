// How far an export has come, and the way to stop it.
//
// A card over the foot of the page, like the bar of a selection and for the same reason: it
// is a layer and takes no room, so the page under it does not move when an export begins. It
// stands there for as long as the export is on its way, and the bubble says how it ended.

import QtQuick
import Leaf

Item {
    id: layer

    objectName: "export-bar"
    anchors.fill: parent
    visible: Exports.going
    z: 41

    Rectangle {
        id: card

        objectName: "export-card"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        // Above the bar of a selection when there is one: a selection can be made while an
        // export goes on, and two cards on one spot hide each other.
        anchors.bottomMargin: Widths.space.gutter + (Selection.active ? Widths.scale.size(56) : 0)
        width: Math.min(layer.width - 2 * Widths.space.gutter, Widths.scale.column(420))
        height: lines.implicitHeight + 2 * Widths.space.row
        radius: Theme.cardRadius
        color: Theme.surface
        border.color: Theme.rule
        border.width: 1
        antialiasing: true

        CardLift { level: CardLift.Bubble }

        Accessible.role: Accessible.ProgressBar
        Accessible.name: Exports.label + " " + Exports.percent

        Column {
            id: lines

            x: Widths.space.row
            y: Widths.space.row
            width: parent.width - 2 * Widths.space.row
            spacing: Widths.scale.room(8)

            Item {
                width: parent.width
                height: Math.max(words.height, stop.height)

                Text {
                    id: words

                    objectName: "export-words"
                    anchors.left: parent.left
                    anchors.right: stop.left
                    anchors.rightMargin: Widths.space.row
                    anchors.verticalCenter: parent.verticalCenter
                    text: Exports.label + "  " + Exports.percent
                    textFormat: Text.PlainText
                    color: Theme.ink
                    elide: Text.ElideRight
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.body(13)
                    font.weight: Font.DemiBold
                }

                LeafTextAction {
                    id: stop

                    objectName: "export-stop"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    label: Exports.words.cancel
                    quiet: true
                    onTriggered: Exports.cancel()
                }
            }

            Rectangle {
                objectName: "export-progress"
                width: parent.width
                height: Widths.scale.size(4)
                radius: height / 2
                color: Theme.rule

                Rectangle {
                    width: parent.width * Exports.fraction
                    height: parent.height
                    radius: parent.radius
                    color: Theme.emerald

                    Behavior on width {
                        NumberAnimation { duration: Preferences.motion === Preferences.Instant ? 0 : 140 }
                    }
                }
            }
        }
    }
}
