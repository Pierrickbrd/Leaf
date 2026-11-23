// Everyone a series is credited with, by role and then by how many volumes each made: what
// « et 9 autres » opens, from the header and from the description alike.
//
// The header names two people and the description three a line; past that, a series taken
// over thirty times by as many writers had no place that said who they were. Read-only, and
// one way out, as `FileInfoDialog` is.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Leaf

LeafDialog {
    id: dialog

    /// `Series.description.people`: `[{ section, name, count }]`, sections in order, worded in C++.
    property var people: []
    property string heading: ""
    property string counted: ""

    objectName: "people-dialog"
    surfaceName: "people-surface"
    initialFocus: closeAction
    drawnWidth: 460

    Column {
        width: parent.width
        spacing: Widths.space.tight

        Text {
            objectName: "people-title"
            width: parent.width
            text: dialog.heading
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: Widths.scale.title(20)
            font.weight: Font.Bold
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
        }

        Text {
            width: parent.width
            text: dialog.counted
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            textFormat: Text.PlainText
        }
    }

    Column {
        width: parent.width
        spacing: Widths.scale.room(6)

        Repeater {
            model: dialog.people.length

            Column {
                id: person

                required property int index
                readonly property var one: dialog.people[index] ?? ({})
                readonly property bool opens: index === 0
                                              || dialog.people[index - 1].section !== one.section

                width: parent.width
                spacing: Widths.scale.room(6)

                Text {
                    visible: person.opens
                    topPadding: person.index === 0 ? 0 : Widths.space.compact
                    text: person.one.section ?? ""
                    color: Theme.inkFaint
                    font.family: Theme.textFamily
                    font.pixelSize: Widths.scale.caption(11)
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.8
                    textFormat: Text.PlainText
                }

                Row {
                    width: parent.width
                    spacing: Widths.space.row

                    Text {
                        objectName: "people-name-" + person.index
                        width: parent.width - count.width - parent.spacing
                        text: person.one.name ?? ""
                        color: Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(14)
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }

                    Text {
                        id: count

                        text: person.one.count ?? ""
                        color: Theme.inkFaint
                        font.family: Theme.textFamily
                        font.pixelSize: Widths.scale.body(14)
                        textFormat: Text.PlainText
                    }
                }
            }
        }
    }

    commands: [
        LeafTextAction {
            id: closeAction

            objectName: "people-close"
            scaled: true
            label: Commands.words.closeInfo
            focus: true
            activeFocusOnTab: true
            anchors.verticalCenter: parent.verticalCenter
            onTriggered: dialog.close()
        }
    ]
}
