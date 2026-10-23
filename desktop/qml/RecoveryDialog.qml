// The recovery code, shown once.
//
// The server sends it a single time and keeps only a digest, so this window is the only chance
// anybody has to write it down: it cannot be reopened, and nothing here keeps it. It closes on
// the one button that says it was noted, and not on a click beside it or Escape — a window that
// vanished at a stray click would have lost the code for good.
//
// Selectable and not copyable on a button: the clipboard outlives this window, and a code that
// is meant to stay off the device is not one to leave in it.

import QtQuick
import QtQuick.Controls
import Leaf

Popup {
    id: dialog

    objectName: "recovery-dialog"

    /// The code as the server sent it, set when the window is asked and cleared when it closes.
    property string code: ""
    /// Said instead of the code when it could not be drawn: the code the person typed is
    /// saved all the same, and this window is where they learn the other half.
    property string problem: ""

    function show(value) {
        dialog.code = value
        dialog.problem = ""
        dialog.open()
    }

    function fail(sentence) {
        dialog.code = ""
        dialog.problem = sentence
        dialog.open()
    }

    width: 440
    anchors.centerIn: Overlay.overlay
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    onClosed: {
        dialog.code = ""
        dialog.problem = ""
    }

    Connections {
        target: Profiles

        function onRecoveryDrawn(code) {
            dialog.show(code)
        }
        function onCodeRecovered(next) {
            dialog.show(next)
        }
        function onRecoveryFailed(sentence) {
            dialog.fail(sentence)
        }
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(12 / 255, 16 / 255, 14 / 255, 0.74)
    }

    background: Rectangle {
        radius: Theme.cardRadius
        color: Theme.surface
        border.color: Theme.rule
        border.width: 1
        antialiasing: true

        CardLift { level: CardLift.Modal }
    }

    contentItem: Item {
        objectName: "recovery-dialog-body"
        implicitHeight: form.height + 44

        Column {
            id: form

            x: 24
            y: 22
            width: parent.width - 48
            spacing: 16

            Glyph {
                anchors.horizontalCenter: parent.horizontalCenter
                side: 36
                source: "assets/icons/lock.svg"
                tint: Theme.emerald
            }

            Text {
                objectName: "recovery-title"
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: Profiles.words.recoveryTitle
                color: Theme.ink
                font.family: Theme.displayFamily
                font.pixelSize: 24
                font.weight: Font.Bold
                wrapMode: Text.Wrap
            }

            Rectangle {
                visible: dialog.problem.length === 0
                anchors.horizontalCenter: parent.horizontalCenter
                width: shown.implicitWidth + 40
                height: 52
                radius: 10
                color: Theme.onBar

                TextEdit {
                    id: shown

                    objectName: "recovery-code"
                    anchors.centerIn: parent
                    readOnly: true
                    selectByMouse: true
                    text: dialog.code
                    color: Theme.ink
                    selectionColor: Theme.emerald
                    selectedTextColor: Theme.onEmerald
                    font.family: Theme.commandFamily
                    font.pixelSize: 20
                    font.weight: Font.Medium
                    font.letterSpacing: 1
                }
            }

            Text {
                objectName: "recovery-explained"
                visible: dialog.problem.length === 0
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: Profiles.words.recoveryExplained
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: 13
                lineHeight: 1.35
                wrapMode: Text.Wrap
            }

            Text {
                objectName: "recovery-problem"
                visible: dialog.problem.length > 0
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: dialog.problem
                color: Theme.alert
                font.family: Theme.textFamily
                font.pixelSize: 13
                lineHeight: 1.35
                wrapMode: Text.Wrap
            }

            ActionButton {
                objectName: "recovery-noted"
                anchors.horizontalCenter: parent.horizontalCenter
                label: Profiles.words.recoveryNoted
                onTriggered: dialog.close()
            }
        }
    }
}
