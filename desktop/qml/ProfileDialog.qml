// One window to make a profile and to change one: a name, a colour among sixteen, and what
// the profile may do — as much of it as the profile that opened the window may decide.
//
// `new` from the principal chooses reader or restricted, the rights or the age, and the code
// when the first restricted profile needs one. `new` from a reader is a name and a colour: a
// new profile from there is a reader with no right, and the principal decides the rest.
// `self` is your own name and colour, and for the principal the code. `edit` is the
// principal changing another profile, like `new` without the code.
//
// Nothing here decides whether a request may go: the server refuses a name or a colour
// already taken, an age out of range, two codes that differ, and the refusal is said in the
// window, which stays open so that nothing typed is lost.

import QtQuick
import QtQuick.Controls
import Leaf

Popup {
    id: dialog

    objectName: "profile-dialog"

    /// `new`, `self` or `edit`.
    property string mode: "new"
    /// The profile changed, from `Profiles.list`; null when making one.
    property var profile: null
    /// The code a reader typed before adding, for the server to check with the request that
    /// creates. Held for the window's lifetime only, and cleared when it closes.
    property string principalCode: ""

    property string name: ""
    property string color: ""
    property string kind: "reader"
    property bool mayImport: false
    property bool mayEdit: false
    property bool mayErase: false
    property int maxAge: 10
    property bool codeOn: false
    property string scope: "restricted"

    /// The requests still to send, one after the other: a second one sent at once would
    /// clear the first one's refusal when its own answer went.
    property var steps: []
    property bool waiting: false
    property string said: ""

    readonly property bool decides: Profiles.isPrincipal && dialog.mode !== "self"
    readonly property bool restricted: dialog.decides && dialog.kind === "restricted"
    /// The first restricted profile makes the code, in the same window — made, or a reader
    /// turned restricted: the code is required from the first one, whichever way it comes.
    readonly property bool makesTheCode: dialog.restricted && Profiles.codeScope === "none"
                                         && (dialog.mode === "new"
                                             || dialog.profile.kind !== "restricted")
    readonly property bool ownsTheCode: dialog.mode === "self" && Profiles.isPrincipal
    /// The code cannot be removed while a profile is restricted: nobody could leave it.
    readonly property var held: Profiles.list.find(one => one.kind === "restricted") ?? null
    readonly property bool asksTwice: dialog.makesTheCode
                                      || (dialog.ownsTheCode && dialog.codeOn)
    /// Making the code, or changing what it is for, means typing it: the server keeps only
    /// its digest and asks for it whole each time.
    readonly property bool codeNeeded: dialog.makesTheCode
                                       || (dialog.ownsTheCode && dialog.codeOn
                                           && (Profiles.codeScope === "none"
                                               || dialog.scope !== Profiles.codeScope))
    /// Changing the code, or taking it off, proves the one in place is known: being the
    /// principal is no proof, since a device that keeps the principal opens on it with no code.
    /// Not for the first code, there being none, and not for what leaves the code alone.
    readonly property bool asksCurrent: dialog.ownsTheCode && Profiles.codeScope !== "none"
                                        && (!dialog.codeOn || code.length > 0
                                            || dialog.scope !== Profiles.codeScope)
    /// Every colour another profile wears, and that profile.
    readonly property var taken: {
        const owners = ({})
        for (const one of Profiles.list) {
            if (!dialog.profile || one.id !== dialog.profile.id)
                owners[one.color] = one
        }
        return owners
    }
    /// One field typed and not the other is a code half-made, whether or not one is needed.
    readonly property bool halfTyped: dialog.asksTwice && (code.length > 0) !== (again.length > 0)
    readonly property var principal: Profiles.list.find(one => one.kind === "principal") ?? null
    readonly property bool ready: dialog.name.trim().length > 0 && dialog.color.length > 0
                                  && !dialog.waiting && !dialog.halfTyped
                                  && (!dialog.codeNeeded || (code.length > 0 && again.length > 0))
                                  && (!dialog.asksCurrent || current.length > 0)

    /// Opens the window for `how` on `target`; `typed` is the code from the code window.
    function show(how, target, typed) {
        dialog.mode = how
        dialog.profile = target
        dialog.principalCode = typed
        dialog.name = target ? target.name : ""
        dialog.kind = target && target.kind === "restricted" ? "restricted" : "reader"
        // Bracketed: `.import` sends the QML compiler into an import statement, and it then
        // refused the next number in the file as a version that was not there.
        dialog.mayImport = !!target && target.rights["import"]
        dialog.mayEdit = !!target && target.rights.edit
        dialog.mayErase = !!target && target.rights.erase
        dialog.maxAge = target && target.maxAge > 0 ? target.maxAge : 10
        dialog.codeOn = Profiles.codeScope !== "none"
        dialog.scope = Profiles.codeScope === "everything" ? "everything" : "restricted"
        dialog.color = target ? target.color
                              : (Theme.profileColors.find(one => dialog.taken[one] === undefined) ?? "")
        dialog.open()
    }

    function submit() {
        if (!dialog.ready)
            return
        const sent = { name: dialog.name.trim(), color: dialog.color }
        if (dialog.mode === "new")
            sent.kind = "reader"
        if (dialog.decides) {
            sent.kind = dialog.kind
            if (dialog.restricted)
                sent.maxAge = dialog.maxAge
            else
                sent.rights = { import: dialog.mayImport, edit: dialog.mayEdit,
                                erase: dialog.mayErase }
        }
        if (dialog.makesTheCode && dialog.mode === "new") {
            sent.code = code.text
            sent.codeAgain = again.text
        }
        if (dialog.principalCode.length > 0)
            sent.principalCode = dialog.principalCode

        const steps = []
        // The code first: the server refuses a restricted profile while there is none.
        if (dialog.makesTheCode && dialog.mode === "edit")
            steps.push(() => Profiles.setCode(code.text, again.text, "restricted", ""))
        if (dialog.mode === "new") {
            steps.push(() => Profiles.add(sent))
        } else if (dialog.mode === "edit" || sent.name !== dialog.profile.name
                   || sent.color !== dialog.profile.color) {
            steps.push(() => Profiles.change(dialog.profile.id, sent))
        }
        if (dialog.ownsTheCode) {
            if (dialog.codeOn && code.length > 0)
                steps.push(() => Profiles.setCode(code.text, again.text, dialog.scope,
                                                  current.text))
            else if (!dialog.codeOn && Profiles.codeScope !== "none")
                steps.push(() => Profiles.clearCode(current.text))
        }
        dialog.said = ""
        dialog.steps = steps
        dialog.next()
    }

    function next() {
        if (dialog.steps.length === 0) {
            dialog.close()
            return
        }
        const step = dialog.steps[0]
        dialog.steps = dialog.steps.slice(1)
        dialog.waiting = true
        step()
    }

    width: 470
    anchors.centerIn: Overlay.overlay
    padding: 0
    modal: true
    focus: true
    closePolicy: dialog.waiting ? Popup.NoAutoClose : Popup.CloseOnEscape
    // Nothing typed outlives the window: neither the codes nor the principal's.
    onClosed: {
        dialog.principalCode = ""
        code.clear()
        again.clear()
        current.clear()
        dialog.steps = []
        dialog.waiting = false
        dialog.said = ""
    }

    Connections {
        target: Profiles

        function onChanged() {
            if (!dialog.waiting || Profiles.busy)
                return
            dialog.waiting = false
            if (Profiles.trouble.length > 0) {
                dialog.steps = []
                dialog.said = Profiles.trouble
                // The steps that went are in the list now: a retry compares with them and
                // does not send again what the server already took.
                if (dialog.profile)
                    dialog.profile = Profiles.list.find(one => one.id === dialog.profile.id)
                                     ?? dialog.profile
            } else {
                dialog.next()
            }
        }
    }

    component Heading: Text {
        color: Theme.ink
        font.family: Theme.textFamily
        font.pixelSize: 13
        font.weight: Font.Medium
    }

    component Note: Text {
        width: parent.width
        // « Le code protège X » names a profile, shown as typed.
        textFormat: Text.PlainText
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: 12
        lineHeight: 1.35
        wrapMode: Text.Wrap
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
        objectName: "profile-dialog-body"
        implicitHeight: form.height + 34 + actions.height + 18

        Column {
            id: form

            x: 20
            y: 18
            width: parent.width - 40
            spacing: 16

            Text {
                objectName: "profile-title"
                width: parent.width
                text: dialog.mode === "self" ? Profiles.words.yourProfile
                      : dialog.mode === "edit" ? Profiles.sayEditProfile(dialog.profile ? dialog.profile.name : "")
                      : Profiles.words.newProfile
                // The name is the profile's own choice, shown as typed, never as markup.
                textFormat: Text.PlainText
                color: Theme.ink
                font.family: Theme.displayFamily
                font.pixelSize: 22
                font.weight: Font.Bold
            }

            Text {
                width: parent.width
                visible: dialog.ownsTheCode
                text: Profiles.words.principalExplained
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: 13
                lineHeight: 1.35
                wrapMode: Text.Wrap
            }

            Row {
                width: parent.width
                spacing: 14

                ProfileSquare {
                    objectName: "profile-preview"
                    profile: ({ color: dialog.color, initial: (Array.from(dialog.name.trim())[0] ?? "").toUpperCase() })
                    side: 56
                    radius: 15
                }

                Column {
                    width: parent.width - 70
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Heading { text: Profiles.words.profileName }

                    LeafField {
                        id: nameField

                        objectName: "profile-name"
                        width: parent.width
                        focus: true
                        text: dialog.name
                        Accessible.name: Profiles.words.profileName
                        onTextEdited: dialog.name = text
                        onAccepted: dialog.submit()
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 8

                Heading { text: Profiles.words.profileColour }

                Grid {
                    columns: 8
                    spacing: 10

                    Repeater {
                        id: swatches

                        model: Theme.profileColors

                        /// The next free swatch from `from`, `step` at a time; `from` itself
                        /// when there is none — a taken colour is not a stop.
                        function moveFocus(from, step) {
                            for (let at = from + step; at >= 0 && at < count; at += step) {
                                if (itemAt(at).enabled) {
                                    itemAt(at).forceActiveFocus()
                                    return
                                }
                            }
                        }

                        delegate: Item {
                            id: swatch

                            required property string modelData
                            required property int index
                            readonly property var holder: dialog.taken[modelData] ?? null
                            readonly property string owner: holder ? holder.initial : ""
                            readonly property color tint: Theme.profileTintIn(Theme.profileTints, modelData)

                            objectName: "profile-colour-" + modelData
                            width: 40
                            height: 40
                            enabled: owner.length === 0
                            activeFocusOnTab: dialog.color === modelData
                            Accessible.role: Accessible.RadioButton
                            Accessible.name: Profiles.sayColourName(modelData, holder ? holder.name : "")
                            Accessible.checkable: true
                            Accessible.checked: dialog.color === modelData
                            Accessible.onPressAction: dialog.color = modelData

                            Rectangle {
                                visible: dialog.color === swatch.modelData
                                anchors.fill: parent
                                radius: 12
                                color: "transparent"
                                border.color: swatch.tint
                                border.width: 2
                                antialiasing: true
                            }

                            Rectangle {
                                anchors.centerIn: parent
                                width: 32
                                height: 32
                                radius: 9
                                antialiasing: true
                                opacity: swatch.owner.length > 0 ? 0.35 : 1
                                color: swatch.tint
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: swatch.owner.length > 0
                                text: swatch.owner
                                color: Theme.ink
                                font.family: Theme.displayFamily
                                font.pixelSize: 17
                                font.weight: Font.Bold
                            }

                            Keys.onPressed: event => {
                                if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                                        || event.key === Qt.Key_Enter) {
                                    dialog.color = swatch.modelData
                                } else if (event.key === Qt.Key_Left) {
                                    swatches.moveFocus(swatch.index, -1)
                                } else if (event.key === Qt.Key_Right) {
                                    swatches.moveFocus(swatch.index, 1)
                                } else if (event.key === Qt.Key_Up) {
                                    swatches.moveFocus(swatch.index, -8)
                                } else if (event.key === Qt.Key_Down) {
                                    swatches.moveFocus(swatch.index, 8)
                                } else {
                                    return
                                }
                                event.accepted = true
                            }

                            FocusRing {
                                objectName: swatch.objectName + "-focus"
                                cornerRadius: 12
                            }

                            HoverHandler { cursorShape: Qt.PointingHandCursor }

                            TapHandler {
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: dialog.color = swatch.modelData
                            }
                        }
                    }
                }

                Note { text: Profiles.words.takenColourNote }
            }

            Column {
                width: parent.width
                visible: dialog.decides
                spacing: 8

                Heading { text: Profiles.words.whatItMayDo }

                SettingsChoice {
                    objectName: "profile-kind"
                    width: parent.width
                    options: [{ value: 0, label: Profiles.words.readerRole, icon: "book" },
                              { value: 1, label: Profiles.words.restrictedRole, icon: "lock" }]
                    chosen: dialog.kind === "restricted" ? 1 : 0
                    onPicked: value => dialog.kind = value === 1 ? "restricted" : "reader"
                }

                Text {
                    width: parent.width
                    text: dialog.restricted ? Profiles.words.restrictedExplained
                                            : Profiles.words.readerExplained
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 12
                    lineHeight: 1.35
                    wrapMode: Text.Wrap
                }

                Row {
                    visible: !dialog.restricted
                    spacing: 4

                    LeafCheck {
                        objectName: "profile-may-import"
                        width: 130
                        label: Profiles.words.mayImport
                        checked: dialog.mayImport
                        onToggled: wanted => dialog.mayImport = wanted
                    }
                    LeafCheck {
                        objectName: "profile-may-edit"
                        width: 130
                        label: Profiles.words.mayEdit
                        checked: dialog.mayEdit
                        onToggled: wanted => dialog.mayEdit = wanted
                    }
                    LeafCheck {
                        objectName: "profile-may-erase"
                        width: 130
                        label: Profiles.words.mayErase
                        checked: dialog.mayErase
                        onToggled: wanted => dialog.mayErase = wanted
                    }
                }
            }

            Row {
                width: parent.width
                visible: dialog.restricted
                spacing: 16

                Column {
                    spacing: 6

                    Heading { text: Profiles.words.seriesUpTo }

                    // Three to seventeen, a year at a time: the server's range.
                    Rectangle {
                        width: 140
                        height: 34
                        radius: 6
                        color: Theme.onBar

                        IconButton {
                            objectName: "profile-age-less"
                            x: 4
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: "remove"
                            label: Profiles.words.fewerYears
                            enabled: dialog.maxAge > 3
                            opacity: enabled ? 1 : 0.4
                            onTriggered: dialog.maxAge -= 1
                        }

                        Text {
                            objectName: "profile-age"
                            anchors.centerIn: parent
                            text: Profiles.sayYearsOld(dialog.maxAge)
                            color: Theme.ink
                            font.family: Theme.textFamily
                            font.pixelSize: 14
                        }

                        IconButton {
                            objectName: "profile-age-more"
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: "add"
                            label: Profiles.words.moreYears
                            enabled: dialog.maxAge < 17
                            opacity: enabled ? 1 : 0.4
                            onTriggered: dialog.maxAge += 1
                        }
                    }
                }

                Column {
                    id: firstCode

                    visible: dialog.makesTheCode
                    width: parent.width - 156
                    spacing: 6

                    Heading { text: Profiles.words.codeToCreate }
                }
            }

            Note {
                visible: dialog.restricted
                text: Profiles.words.ageLimitNote
            }

            Note {
                visible: dialog.makesTheCode
                text: Profiles.words.firstCodeHint
            }

            Column {
                width: parent.width
                visible: dialog.ownsTheCode
                spacing: 10

                Item {
                    width: parent.width
                    height: 30

                    Heading {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: Profiles.words.aCode
                    }

                    LeafSwitch {
                        objectName: "profile-code-switch"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        label: Profiles.words.aCode
                        on: dialog.codeOn
                        enabled: !dialog.held
                        opacity: enabled ? 1 : 0.45
                        onToggled: wanted => dialog.codeOn = wanted
                    }
                }

                Item {
                    id: ownCode

                    width: parent.width
                    height: 34
                    visible: dialog.codeOn
                }

                Heading {
                    visible: dialog.codeOn
                    text: Profiles.words.whenCodeAsked
                }

                SettingsChoice {
                    objectName: "profile-code-scope"
                    width: parent.width
                    visible: dialog.codeOn
                    options: [{ value: 0, label: Profiles.words.toLeaveRestricted, icon: "lock" },
                              { value: 1, label: Profiles.words.alsoToComeBack, icon: "crown" }]
                    chosen: dialog.scope === "everything" ? 1 : 0
                    onPicked: value => dialog.scope = value === 1 ? "everything" : "restricted"
                }

                LeafField {
                    id: current

                    objectName: "profile-code-current"
                    width: parent.width
                    visible: dialog.asksCurrent
                    echoMode: TextInput.Password
                    maximumLength: 8
                    placeholderText: Profiles.words.currentCode
                    Accessible.name: Profiles.words.currentCode
                    onAccepted: dialog.submit()
                }

                Note {
                    objectName: "profile-code-current-note"
                    visible: dialog.asksCurrent
                    text: Profiles.words.currentCodeAsked
                }

                Note {
                    objectName: "profile-code-hint"
                    text: dialog.held ? Profiles.sayCodeHintHeld(dialog.held.name)
                                      : Profiles.words.codeHintFree
                }
            }

            Text {
                objectName: "profile-manages"
                width: parent.width
                visible: dialog.mode === "new" && !Profiles.isPrincipal
                text: Profiles.sayPrincipalManages(dialog.principal ? dialog.principal.name : "")
                textFormat: Text.PlainText
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: 12
                lineHeight: 1.35
                wrapMode: Text.Wrap
            }

            Text {
                objectName: "profile-trouble"
                width: parent.width
                visible: dialog.said.length > 0
                text: dialog.said
                color: Theme.alert
                font.family: Theme.textFamily
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
        }

        // The two code fields, the same two wherever the code is typed: under « Le code, à
        // créer » side by side with the age, or under « Un code » side by side with each other.
        Column {
            parent: dialog.makesTheCode ? firstCode : ownCode
            width: parent ? parent.width : 0
            visible: dialog.asksTwice

            Flow {
                width: parent.width
                spacing: 10

                LeafField {
                    id: code

                    objectName: "profile-code"
                    width: dialog.makesTheCode ? parent.width : (parent.width - 10) / 2
                    echoMode: TextInput.Password
                    maximumLength: 8
                    placeholderText: Profiles.words.codeFirst
                    Accessible.name: dialog.makesTheCode ? Profiles.words.codeToCreate
                                                         : Profiles.words.aCode
                    onAccepted: dialog.submit()
                }

                LeafField {
                    id: again

                    objectName: "profile-code-again"
                    width: code.width
                    echoMode: TextInput.Password
                    maximumLength: 8
                    placeholderText: Profiles.words.codeAgain
                    Accessible.name: Profiles.words.codeAgain
                    onAccepted: dialog.submit()
                }
            }
        }

        Row {
            id: actions

            anchors.right: form.right
            anchors.top: form.bottom
            anchors.topMargin: 18
            spacing: 8

            LeafTextAction {
                objectName: "profile-cancel"
                anchors.verticalCenter: parent.verticalCenter
                label: Profiles.words.cancel
                enabled: !dialog.waiting
                onTriggered: dialog.close()
            }

            ActionButton {
                objectName: "profile-confirm"
                label: dialog.mode === "new" ? Profiles.sayAddProfileButton(dialog.name.trim())
                                             : Profiles.words.saveProfile
                ready: dialog.ready
                onTriggered: dialog.submit()
            }
        }
    }
}
