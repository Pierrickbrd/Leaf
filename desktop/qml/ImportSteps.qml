// The three steps of an import, numbered, over the window: Choisir, Vérifier, Envoyer.
//
// Always the same three, even when the second is skipped: a complete folder goes from the
// first to the third, and the second stays drawn as done rather than disappearing — which is
// what tells a reader that nothing was hidden from them, only nothing asked.
//
// A step behind is a ring on the emerald's wash, the step in force the emerald itself, a step
// ahead a ring of the rule.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Row {
    id: steps

    /// From 1 to 3.
    required property int current

    spacing: Widths.scale.room(20)

    Accessible.role: Accessible.List

    Repeater {
        model: ImportCaptions.stepLabels

        delegate: Row {
            id: step

            required property int index
            required property string modelData

            readonly property int number: index + 1
            readonly property bool done: number < steps.current
            readonly property bool on: number === steps.current

            spacing: Widths.space.label

            Accessible.role: Accessible.ListItem
            Accessible.name: step.modelData

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(Widths.scale.fit(22), figure.implicitHeight + Widths.scale.fit(6))
                height: width
                radius: width / 2
                antialiasing: true
                color: step.on ? Theme.emerald : step.done ? Theme.emeraldWash : "transparent"
                border.width: 1
                border.color: step.on || step.done ? Theme.emerald : Theme.rule

                Text {
                    id: figure

                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: step.number
                    color: step.on ? Theme.onEmerald : step.done ? Theme.emerald : Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(12)
                    font.weight: step.on ? Font.DemiBold : Font.Normal
                }
            }

            Text {
                objectName: step.on ? "import-steps-on" : "import-steps-" + step.number
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: step.modelData
                color: step.on ? Theme.ink : Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.body(13)
                font.weight: step.on ? Font.DemiBold : Font.Medium
            }
        }
    }
}
