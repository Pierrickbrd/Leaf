// The code, asked before a change of profile it guards: leaving a restricted profile, coming
// back to a principal who protected the way back, adding a profile from another one.
//
// Eight rounds whatever the code's length, so that the screen never says how long it is. The
// server checks it: a switch sends it with the change, and adding hands it to the profile
// window, whose own request is the one that carries it — so nothing here knows whether a code
// is right until the server has said.

import QtQuick
import QtQuick.Controls
import Leaf

Popup {
    id: dialog

    objectName: "code-dialog"

    /// The profile left, as `Profiles.active`; empty for a device whose profile was deleted.
    property var from: ({})
    /// The profile asked for, from `Profiles.list`; null when adding.
    property var to: null
    /// `switch` or `add`.
    property string mode: "switch"
    /// Whether the switch, once allowed, makes this device open on `to`.
    property bool keep: false
    property bool forgot: false
    /// What the last try came back with: the wrong code, or the server's own pause.
    property string said: ""
    property bool waiting: false

    /// Adding: the code as typed, for the profile window to send.
    signal codeTyped(string code)

    /// A new code by the recovery code, from the window of a forgotten one. The server says
    /// whether the recovery code is right; nothing here does.
    function recover() {
        if (dialog.waiting || recoveryField.text.length === 0 || newCode.text.length === 0)
            return
        dialog.said = ""
        dialog.waiting = true
        Profiles.recover(recoveryField.text, newCode.text, newCodeAgain.text)
    }

    function ask(target, how, keeping) {
        dialog.from = Profiles.active
        dialog.to = target
        dialog.mode = how
        dialog.keep = keeping
        dialog.open()
    }

    function submit() {
        if (field.text.length === 0 || dialog.waiting)
            return
        dialog.said = ""
        if (dialog.mode === "add") {
            const code = field.text
            dialog.close()
            dialog.codeTyped(code)
            return
        }
        dialog.waiting = true
        Profiles.switchTo(dialog.to.id, field.text, dialog.keep)
    }

    width: 400
    anchors.centerIn: Overlay.overlay
    padding: 0
    modal: true
    focus: true
    // Not while a switch is out: a window cancelled and then switched anyway by the answer
    // would have lied about what « Annuler » did.
    closePolicy: dialog.waiting ? Popup.NoAutoClose : Popup.CloseOnEscape
    // Nothing typed outlives the window, nor the sentence about the last try.
    onClosed: {
        field.clear()
        recoveryField.clear()
        newCode.clear()
        newCodeAgain.clear()
        dialog.forgot = false
        dialog.said = ""
        dialog.waiting = false
    }

    Connections {
        target: Profiles

        function onSwitched() {
            dialog.close()
        }
        // The code is replaced: the window that shows the next recovery code takes over.
        function onCodeRecovered() {
            dialog.close()
        }
        function onCodeRefused() {
            if (dialog.visible)
                field.clear()
        }
        // A refusal that is not about the code — ten wrong tries and the pause after them,
        // a server gone — is said in the same place, in the server's words.
        function onChanged() {
            if (!dialog.waiting || Profiles.busy)
                return
            dialog.waiting = false
            dialog.said = Profiles.trouble
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
        objectName: "code-dialog-body"
        implicitHeight: form.height + 44

        Column {
            id: form

            x: 24
            y: 22
            width: parent.width - 48
            spacing: 16

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 14

                ProfileSquare {
                    objectName: "code-from"
                    visible: !!dialog.from && !!dialog.from.id
                    profile: dialog.from
                    side: 52
                    radius: 14
                }

                Glyph {
                    visible: dialog.mode === "switch" && !!dialog.from && !!dialog.from.id
                    anchors.verticalCenter: parent.verticalCenter
                    side: 22
                    source: "assets/icons/arrow_back.svg"
                    tint: Theme.inkSoft
                    rotation: 180
                }

                ProfileSquare {
                    objectName: "code-to"
                    visible: dialog.mode === "switch"
                    profile: dialog.to ?? ({})
                    side: 52
                    radius: 14
                }
            }

            Text {
                objectName: "code-title"
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: dialog.mode === "add" ? Profiles.words.addAProfile
                                            : Profiles.saySwitchToProfile(dialog.to ? dialog.to.name : "")
                // The name is the profile's own choice, shown as typed, never as markup.
                textFormat: Text.PlainText
                color: Theme.ink
                font.family: Theme.displayFamily
                font.pixelSize: 24
                font.weight: Font.Bold
                wrapMode: Text.Wrap
            }

            Text {
                objectName: "code-sentence"
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: dialog.forgot ? Profiles.words.recoverExplained
                      : dialog.mode === "add" ? Profiles.words.addNeedsCode
                      : (Profiles.isRestricted || Profiles.held) ? Profiles.words.leaveNeedsCode
                      : Profiles.words.enterCode
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: 13
                lineHeight: 1.35
                wrapMode: Text.Wrap
            }

            // Typed apart and never seen: the recovery code is a secret like the code, and the
            // two new codes are what it is replaced with.
            Column {
                objectName: "code-recover-fields"
                visible: dialog.forgot
                width: parent.width
                spacing: 10

                LeafField {
                    id: recoveryField

                    objectName: "code-recovery-field"
                    width: parent.width
                    placeholderText: Profiles.words.recoveryField
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                    Accessible.name: Profiles.words.recoveryField
                    onTextEdited: dialog.said = ""
                    onAccepted: newCode.forceActiveFocus()
                }

                Row {
                    width: parent.width
                    spacing: 10

                    LeafField {
                        id: newCode

                        objectName: "code-new-field"
                        width: (parent.width - 10) / 2
                        echoMode: TextInput.Password
                        maximumLength: 8
                        placeholderText: Profiles.words.newCodeField
                        Accessible.name: Profiles.words.newCodeField
                        onTextEdited: dialog.said = ""
                        onAccepted: newCodeAgain.forceActiveFocus()
                    }

                    LeafField {
                        id: newCodeAgain

                        objectName: "code-new-again-field"
                        width: newCode.width
                        echoMode: TextInput.Password
                        maximumLength: 8
                        placeholderText: Profiles.words.codeAgain
                        Accessible.name: Profiles.words.codeAgain
                        onTextEdited: dialog.said = ""
                        onAccepted: dialog.recover()
                    }
                }
            }

            Text {
                objectName: "code-no-recovery"
                visible: dialog.forgot
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: Profiles.words.noRecoveryCommand
                color: Theme.inkFaint
                font.family: Theme.textFamily
                font.pixelSize: 12
                lineHeight: 1.35
                wrapMode: Text.Wrap
            }

            Rectangle {
                visible: dialog.forgot
                anchors.horizontalCenter: parent.horizontalCenter
                width: command.implicitWidth + 28
                height: 36
                radius: 8
                color: Theme.onBar

                // Selectable, to be copied into the server's terminal as it stands.
                TextEdit {
                    id: command

                    objectName: "code-command"
                    anchors.centerIn: parent
                    readOnly: true
                    selectByMouse: true
                    text: Profiles.words.resetCodeCommand
                    color: Theme.ink
                    selectionColor: Theme.emerald
                    selectedTextColor: Theme.onEmerald
                    font.family: Theme.commandFamily
                    font.pixelSize: 13
                }
            }

            Item {
                objectName: "code-rounds"
                visible: !dialog.forgot
                anchors.horizontalCenter: parent.horizontalCenter
                width: rounds.width
                height: rounds.height

                // Typed into, never seen: the rounds are what shows. Echo off and the input
                // method told it is sensitive, so no prediction keeps what is typed.
                TextInput {
                    id: field

                    objectName: "code-field"
                    anchors.fill: parent
                    // Held from the moment the window takes the focus, not from `opened`: that
                    // comes after the fade, and the first character typed before it was lost.
                    focus: true
                    opacity: 0
                    maximumLength: 8
                    echoMode: TextInput.NoEcho
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                    Accessible.name: Profiles.words.enterCode
                    onTextEdited: dialog.said = ""
                    onAccepted: dialog.submit()
                }

                Row {
                    id: rounds

                    spacing: 12

                    Repeater {
                        model: 8

                        delegate: Rectangle {
                            required property int index
                            readonly property bool filled: index < field.length

                            objectName: "code-round-" + index
                            width: 14
                            height: 14
                            radius: 7
                            color: filled ? Theme.ink : "transparent"
                            border.color: filled ? Theme.ink : Theme.inkSoft
                            border.width: 1.5
                            antialiasing: true
                        }
                    }
                }
            }

            Text {
                objectName: "code-trouble"
                visible: text.length > 0
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: dialog.said
                color: Theme.alert
                font.family: Theme.textFamily
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 10

                LeafTextAction {
                    objectName: "code-forgot"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !dialog.forgot
                    label: Profiles.words.forgotCode
                    onTriggered: dialog.forgot = true
                }

                LeafTextAction {
                    objectName: "code-cancel"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !dialog.forgot
                    label: Profiles.words.cancel
                    enabled: !dialog.waiting
                    onTriggered: dialog.close()
                }

                LeafTextAction {
                    objectName: "code-back"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: dialog.forgot
                    label: Profiles.words.back
                    enabled: !dialog.waiting
                    onTriggered: {
                        dialog.forgot = false
                        dialog.said = ""
                        field.forceActiveFocus()
                    }
                }

                ActionButton {
                    objectName: "code-replace"
                    visible: dialog.forgot
                    label: Profiles.words.replaceCode
                    ready: recoveryField.length > 0 && newCode.length > 0 && !dialog.waiting
                    onTriggered: dialog.recover()
                }

                ActionButton {
                    objectName: "code-confirm"
                    visible: !dialog.forgot
                    label: Profiles.words.confirm
                    ready: field.length > 0 && !dialog.waiting
                    onTriggered: dialog.submit()
                }
            }
        }
    }
}
