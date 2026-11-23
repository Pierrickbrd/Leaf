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

LeafDialog {
    id: dialog

    objectName: "recovery-dialog"
    initialFocus: noted

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

    drawnWidth: 440
    lineSpacing: Widths.space.block
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
        textFormat: Text.PlainText
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

        FocusRing {
            cornerRadius: 10
            focused: shown.activeFocus
        }

        TextEdit {
            id: shown

            objectName: "recovery-code"
            anchors.centerIn: parent
            readOnly: true
            selectByMouse: true
            // Selected by the keys as by the hand: Tab reaches it and takes it whole.
            activeFocusOnTab: true
            onActiveFocusChanged: activeFocus ? selectAll() : deselect()
            textFormat: Text.PlainText
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
        textFormat: Text.PlainText
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
        textFormat: Text.PlainText
        text: dialog.problem
        color: Theme.alert
        font.family: Theme.textFamily
        font.pixelSize: 13
        lineHeight: 1.35
        wrapMode: Text.Wrap
    }

    ActionButton {
        id: noted

        objectName: "recovery-noted"
        anchors.horizontalCenter: parent.horizontalCenter
        // The one thing to do here, and Escape does not do it: without the focus,
        // Enter did nothing either.
        focus: true
        label: Profiles.words.recoveryNoted
        onTriggered: dialog.close()
    }
}
