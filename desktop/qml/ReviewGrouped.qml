// What the ticked series take together, in the sheet's place while the last gesture is a tick:
// a direction, a language, a status, each one « Ne pas changer » until chosen, and one press
// that writes them all — in every ticked series, in one batch (`Review.ticks.applyToTicked`).
//
// The choices wait here, in the screen, until that press: written as each was picked, twenty
// series were announced again for a direction, and again for the language picked a moment
// later. A click on a line puts its sheet back; the ticks stay.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Item {
    id: panel

    /// Read once and only while it is shown, for the reason `ReviewList` gives.
    readonly property var grouped: panel.visible ? Review.ticks.grouped : ({})
    readonly property var fields: panel.grouped.fields ?? []
    /// The choice waiting on each field, by key: a place among its `choices`, 0 for none.
    property var chosen: ({})
    readonly property bool anything: Object.keys(panel.chosen).some(key => panel.chosen[key] > 0)

    function choose(key: string, at: int): void {
        const next = Object.assign({}, panel.chosen)
        next[key] = at
        panel.chosen = next
    }

    function apply(): void {
        const values = {}
        for (const field of panel.fields) {
            const at = panel.chosen[field.key] ?? 0
            if (at > 0)
                values[field.key] = field.choices[at].value
        }
        Review.ticks.applyToTicked(values)
    }

    // Shut, it forgets what was waiting: the next ticks are other series, and a direction
    // left picked for the last ones would be applied to them by a press meant for something
    // else.
    onVisibleChanged: {
        if (!visible)
            panel.chosen = ({})
    }

    clip: true

    Accessible.role: Accessible.Pane
    Accessible.name: panel.grouped.title ?? ""

    Item {
        id: heading

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Widths.space.card
        anchors.bottomMargin: 0
        height: title.implicitHeight

        Text {
            id: title

            objectName: "review-grouped-title"
            anchors.left: parent.left
            anchors.right: aside.left
            anchors.rightMargin: Widths.space.label
            anchors.top: parent.top
            text: panel.grouped.title ?? ""
            textFormat: Text.PlainText
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: Widths.scale.title(26)
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        // On the title's baseline, as the sheet's subtitle.
        Text {
            id: aside

            anchors.right: parent.right
            anchors.baseline: title.baseline
            width: Math.min(implicitWidth, heading.width * 0.55)
            text: panel.grouped.aside ?? ""
            textFormat: Text.PlainText
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
        }
    }

    Flickable {
        id: flick

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.bottom: note.top
        anchors.topMargin: Widths.space.row
        anchors.leftMargin: Widths.space.card
        anchors.rightMargin: Widths.space.card
        anchors.bottomMargin: Widths.space.row
        contentHeight: column.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: column

            width: flick.width
            spacing: Widths.space.row

            Repeater {
                model: panel.fields.length

                delegate: LeafSelect {
                    id: select

                    required property int index
                    readonly property var field: panel.fields[index]
                                                 ?? ({ key: "", label: "", choices: [] })

                    objectName: "review-grouped-" + select.field.key
                    width: column.width
                    name: select.field.label
                    edged: true
                    options: select.field.choices.map((choice, at) => ({ value: at,
                                                                         label: choice.label }))
                    chosen: panel.chosen[select.field.key] ?? 0
                    onPicked: value => panel.choose(select.field.key, value)
                }
            }

            Text {
                objectName: "review-grouped-outcome"
                width: column.width
                visible: text.length > 0
                text: panel.grouped.outcome ?? ""
                textFormat: Text.PlainText
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                wrapMode: Text.WordWrap

                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            ActionButton {
                objectName: "review-apply"
                scaled: true
                height: sized(28)
                ready: panel.anything
                label: panel.grouped.applyLabel ?? ""
                onTriggered: panel.apply()
            }
        }
    }

    FollowsFocus {
        flick: flick
    }

    // Why a name or a number is never offered here, in the quiet box every aside of the
    // frame sits in.
    Rectangle {
        id: note

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Widths.space.card
        height: noteText.implicitHeight + 2 * Widths.space.row
        radius: Widths.scale.size(10)
        color: Theme.onPaper
        border.color: Theme.rule
        border.width: 1

        Text {
            id: noteText

            objectName: "review-grouped-note"
            anchors.fill: parent
            anchors.margins: Widths.space.row
            text: panel.grouped.note ?? ""
            textFormat: Text.PlainText
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            lineHeight: 1.3
            wrapMode: Text.WordWrap

            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }
}
