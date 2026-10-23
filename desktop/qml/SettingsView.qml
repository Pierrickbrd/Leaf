// What you can change, and what merely is.
//
// Two columns, and the split is not decorative: the left is what a reader acts on, the
// right is what the library is doing. Reading order puts the thing you came to do first,
// and a state you cannot change has no business competing with it. Below the wide band
// they stack, the actions still first.
//
// **What is not here.** The server's version and where the key sits — a reader does not need
// them, and this is a client for reading comics. They appear under the connection and only
// when it is broken, which is the one moment they help rather than clutter. The address is
// the exception, on the library tab: it is where this device was linked, and the one thing
// a person may have to correct there — with the name, the unlinking and the linking of
// another device beside it.

import QtQuick
import QtQuick.Controls
import Leaf

Item {
    id: page

    readonly property int general: 0
    readonly property int notifications: 1
    readonly property int library: 2
    property int section: general
    /// The name or the address being typed in, in place of the line that shows it.
    property bool renaming: false
    property bool moving: false

    readonly property bool sideBySide: Widths.band === Widths.Wide

    objectName: "settings-view"
    clip: true

    Component.onCompleted: {
        Health.ask()
        Scan.ask()
    }

    // The sentence a clicked link was answered with, kept only while the settings are open:
    // it was for the person who was sent here.
    // So is what renaming, moving or unlinking was answered.
    Component.onDestruction: {
        Linking.clearNotice()
        Bond.clearSentence()
    }

    // The library answers at the new address: what this screen knows of it was asked of the
    // old one.
    Connections {
        target: Bond

        function onMoved() {
            page.moving = false
            Health.ask()
        }

        // The server refused to relay: whatever the tab offers was decided on an older answer.
        function onNobodyElseReaches() {
            Health.ask()
        }
    }

    function takeFocus(forward) {
        const tab = tabs.tabAt(forward ? 0 : Settings.sections.length - 1)
        if (!tab)
            return false
        tab.forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
        return true
    }

    SettingsTabs {
        id: tabs

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        choices: Settings.sections
        currentIndex: page.section
        onSelected: index => page.section = index
        onWentBack: Navigation.back()
    }

    Flickable {
        id: sheet

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: columns.height + 2 * page.margin
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Item {
            id: columns

            x: page.margin
            y: page.margin
            width: sheet.width - 2 * page.margin
            height: Math.max(acted.height, observed.height)
                    + (page.sideBySide ? 0 : observed.height > 0 ? page.gap : 0)

            // Halves, because that is what two columns means. The left one holds what
            // you act on and the right what the library is doing; neither earns more of
            // the page than the other.
            readonly property real side: page.sideBySide
                                         ? Math.round((width - page.gap) / 2) : width

            Column {
                id: acted

                objectName: "settings-actions"
                width: columns.side
                spacing: page.gap

                // First in the column, above the cards: it is what the click that sent the
                // person here was answered.
                SettingsNote {
                    objectName: "settings-already-linked"
                    visible: page.section === page.library && Linking.notice.length > 0
                    text: Linking.notice
                }

                SettingsCard {
                    objectName: "settings-appearance"
                    visible: page.section === page.general
                    title: Preferences.appearanceTitle

                    SettingsChoice {
                        objectName: "appearance"
                        width: parent.width
                        options: Preferences.appearances
                        chosen: Preferences.appearance
                        onPicked: value => Preferences.chooseAppearance(value)
                    }
                }

                // Four lines and two columns of switches, in one card. Four cards of three
                // pills would have filled the section to say the same thing, and a line makes
                // room for a fifth family the day one asks for it.
                SettingsCard {
                    objectName: "settings-warnings"
                    visible: page.section === page.notifications
                    title: Preferences.warningsTitle

                    Item {
                        width: parent.width
                        height: 18

                        Text {
                            x: heads.switches
                            width: 54
                            horizontalAlignment: Text.AlignHCenter
                            text: Preferences.bubbleColumn
                            color: Theme.inkFaint
                            font.family: Theme.textFamily
                            font.pixelSize: 10
                            font.capitalization: Font.AllUppercase
                        }

                        Text {
                            x: heads.switches + 54
                            width: 54
                            horizontalAlignment: Text.AlignHCenter
                            text: Preferences.desktopColumn
                            color: Theme.inkFaint
                            font.family: Theme.textFamily
                            font.pixelSize: 10
                            font.capitalization: Font.AllUppercase
                        }
                    }

                    Column {
                        id: heads

                        width: parent.width
                        spacing: 10

                        /// Where both columns of switches begin, so every line lines up
                        /// whatever the words beside them are.
                        readonly property real switches: width - 108

                        Repeater {
                            // A count, and each line reads its family by index. Handed the
                            // list itself, the repeater rebuilt all four lines every time one
                            // switch was pressed — the list is a new value on every change —
                            // and the switch pressed was thrown away and replaced by one
                            // already at its other end: no travel, however long it was set to.
                            model: Preferences.warnings.length

                            WarningLine {
                                required property int index
                                readonly property var family: Preferences.warnings[index]

                                objectName: "warns-" + family.name
                                width: heads.width
                                switches: heads.switches
                                label: family.label
                                detail: family.detail
                                bubble: family.bubble
                                desktop: family.desktop
                                onBubbleAsked: wanted =>
                                    Preferences.showBubble(family.value, wanted)
                                onDesktopAsked: wanted =>
                                    Preferences.reachTheDesktop(family.value, wanted)
                            }
                        }
                    }
                }

                // The link itself, after the design's « 06 — une fois relié »: where, as whom,
                // since when — and the two things a person may do to it.
                SettingsCard {
                    objectName: "settings-bond"
                    visible: page.section === page.library
                    title: Bond.title

                    SettingsStatus {
                        objectName: "bond-address"
                        width: parent.width
                        icon: Health.reachable ? "dns" : "cloud_off"
                        label: Bond.address
                        detail: Health.reachable ? Health.holds
                                                 : (Health.asking ? "" : page.whatIsWrong())
                        alarming: !Health.reachable && !Health.asking
                    }

                    Item {
                        objectName: "bond-name"
                        width: parent.width
                        height: 30
                        visible: !page.renaming

                        Text {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: Bond.deviceLabel
                            color: Theme.ink
                            font.family: Theme.textFamily
                            font.pixelSize: 14
                        }

                        Row {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6

                            Text {
                                objectName: "bond-name-value"
                                anchors.verticalCenter: parent.verticalCenter
                                text: Bond.deviceName
                                color: Theme.inkSoft
                                font.family: Theme.textFamily
                                font.pixelSize: 14
                            }

                            IconButton {
                                objectName: "bond-rename"
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: "edit"
                                label: Bond.renameLabel
                                side: 16
                                tint: Theme.emerald
                                onTriggered: {
                                    renameField.text = Bond.deviceName
                                    page.renaming = true
                                    renameField.forceActiveFocus()
                                    renameField.selectAll()
                                }
                            }
                        }
                    }

                    // Enter sends it, Escape leaves the name as it was. What the server
                    // answers comes back in the sentence below.
                    LeafField {
                        id: renameField

                        objectName: "bond-rename-field"
                        width: parent.width
                        visible: page.renaming
                        onAccepted: {
                            Bond.rename(text)
                            page.renaming = false
                        }
                        Keys.onEscapePressed: page.renaming = false
                    }

                    SettingsLine {
                        objectName: "bond-linked"
                        width: parent.width
                        visible: Bond.linkedOn.length > 0
                        label: Bond.linkedLabel
                        value: Bond.linkedOn
                    }

                    // Another device, linked from this one: the panel asks for its code. Only
                    // from a library closed to strangers, the one kind that issues codes, and
                    // reached by other devices, the one kind with an address to give them.
                    ActionButton {
                        objectName: "bond-relay"
                        visible: Health.linksOthers
                        label: Bond.relayLabel
                        onTriggered: relay.open()

                        Glyph {
                            source: "assets/icons/add_link.svg"
                            side: 16
                            tint: Theme.onEmerald
                        }
                    }

                    Item {
                        width: parent.width
                        height: 34
                        visible: page.moving

                        LeafField {
                            id: moveField

                            objectName: "bond-move-field"
                            anchors.left: parent.left
                            anchors.right: tryIt.left
                            anchors.rightMargin: 10
                            placeholderText: Bond.movePlaceholder
                            enabled: !Bond.busy
                            onAccepted: Bond.move(text)
                            Keys.onEscapePressed: page.moving = false
                        }

                        ActionButton {
                            id: tryIt

                            objectName: "bond-move-try"
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            label: Bond.moveAction
                            ready: !Bond.busy && moveField.text.trim().length > 0
                            onTriggered: Bond.move(moveField.text)
                        }
                    }

                    // Nobody reaches this library but the machine it runs on: neither relaying
                    // nor unlinking, and one plain sentence in their place.
                    SettingsNote {
                        objectName: "bond-only-here"
                        width: parent.width
                        visible: Health.onlyHere.length > 0
                        text: Health.onlyHere
                    }

                    SettingsNote {
                        objectName: "bond-sentence"
                        width: parent.width
                        visible: Bond.sentence.length > 0
                        text: Bond.sentence
                        alarming: !Bond.busy
                    }

                    // Back by the actions' own inset, so their words line up with the
                    // labels above rather than with the hover behind them.
                    Row {
                        x: -8
                        spacing: 8

                        LeafTextAction {
                            objectName: "bond-move"
                            label: Bond.moveLabel
                            onTriggered: {
                                moveField.text = Bond.address
                                page.moving = !page.moving
                                if (page.moving)
                                    moveField.forceActiveFocus()
                            }
                        }

                        LeafTextAction {
                            objectName: "bond-unlink"
                            visible: Health.unlinks
                            label: Bond.unlinkLabel
                            alarming: true
                            onTriggered: unlinking.open()
                        }
                    }
                }

                SettingsCard {
                    objectName: "settings-scan"
                    visible: page.section === page.library
                    title: Scan.title

                    SettingsStatus {
                        objectName: "scan-state"
                        width: parent.width
                        icon: "refresh"
                        label: Scan.stateLabel
                        detail: Scan.lastScanLabel
                    }

                    LeafTextAction {
                        objectName: "settings-scan-start"
                        label: Scan.startLabel
                        // While one runs there is nothing to ask for: the state says so, and
                        // a second scan over one library is the same work done twice.
                        visible: !Scan.running && Health.reachable
                        onTriggered: Scan.start()
                    }

                    SettingsNote {
                        objectName: "settings-scan-failure"
                        width: parent.width
                        visible: Scan.failure.length > 0
                        text: Scan.failure
                        alarming: true
                    }

                    SettingsNote {
                        objectName: "settings-scan-trouble"
                        width: parent.width
                        visible: Scan.trouble.length > 0
                        text: Scan.trouble
                        alarming: true
                    }
                }
            }

            Column {
                id: observed

                objectName: "settings-state"
                x: page.sideBySide ? columns.side + page.gap : 0
                y: page.sideBySide ? 0 : acted.height + page.gap
                width: page.sideBySide ? columns.width - columns.side - page.gap
                                       : columns.width
                spacing: page.gap

                // Not drawn at all when nothing makes a bubble: an empty heading is worse
                // than an absent one, which is the rule the shelf already applies to its
                // search sections.
                SettingsCard {
                    objectName: "settings-corner"
                    visible: page.section === page.notifications
                             && Preferences.anythingBubbles
                    title: Preferences.cornerTitle

                    BubbleCorner {
                        objectName: "bubble-corner"
                        width: parent.width
                        chosen: Preferences.corner
                        label: Preferences.cornerLabel
                        onPicked: where => Preferences.putBubbles(where)
                    }
                }

                SettingsCard {
                    objectName: "settings-connection"
                    visible: page.section === page.general
                    title: Health.title

                    SettingsStatus {
                        objectName: "connection"
                        width: parent.width
                        icon: Health.reachable ? "cloud_done" : "cloud_off"
                        label: Health.connected
                        // The address and what the setup is missing, and only when it is:
                        // a reader with a working library has no use for either.
                        detail: Health.reachable ? Health.holds : page.whatIsWrong()
                        alarming: !Health.reachable && !Health.asking
                    }
                }

                SettingsCard {
                    objectName: "settings-found"
                    visible: page.section === page.library && Scan.counts.length > 0
                    title: Scan.foundTitle

                    SettingsLine {
                        objectName: "settings-scan-counts"
                        width: parent.width
                        label: Scan.counts
                        value: Scan.reanalysed
                        strong: true
                    }

                    // Above the chapters without a start page, because a reading position
                    // that moved is the one line of this card worth reading twice.
                    SettingsNote {
                        objectName: "settings-scan-places"
                        width: parent.width
                        visible: Scan.placesCarried.length > 0
                        text: Scan.placesCarried
                    }

                    SettingsNote {
                        objectName: "settings-scan-pages"
                        width: parent.width
                        visible: Scan.withoutStartPage.length > 0
                        text: Scan.withoutStartPage
                    }
                }

                // One card per kind of thing the scan could not make sense of. The scanner
                // reports rather than guesses, and until this screen nothing read what it
                // said.
                Repeater {
                    model: page.section === page.library ? Scan.findings : []

                    delegate: SettingsCard {
                        required property var modelData

                        title: modelData.title

                        Repeater {
                            model: modelData.items

                            delegate: SettingsNote {
                                required property string modelData

                                text: modelData
                            }
                        }

                        SettingsNote {
                            text: modelData.more
                            visible: modelData.more.length > 0
                            muted: true
                        }
                    }
                }
            }
        }
    }

    // « Délier cet appareil ? », after the design: what goes, how to come back, and what to
    // do on the server should it not answer — this device forgets either way.
    Popup {
        id: unlinking

        objectName: "unlink-dialog"
        width: Math.min(430, Overlay.overlay ? Overlay.overlay.width - 80 : 430)
        anchors.centerIn: Overlay.overlay
        padding: 0
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape

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

        Column {
            id: unlinkBody

            x: 17
            y: 16
            width: parent.width - 34
            spacing: 10

            Text {
                objectName: "unlink-question"
                width: parent.width
                text: Bond.unlinkQuestion
                color: Theme.ink
                font.family: Theme.displayFamily
                font.pixelSize: 20
                font.weight: Font.Bold
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: [Bond.unlinkWhat, Bond.unlinkComeBack, Bond.unlinkIfSilent]

                delegate: Text {
                    required property string modelData

                    width: unlinkBody.width
                    text: modelData
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 12
                    lineHeight: 1.4
                    wrapMode: Text.WordWrap
                }
            }
        }

        Row {
            anchors.right: unlinkBody.right
            anchors.top: unlinkBody.bottom
            anchors.topMargin: 16
            spacing: 8

            LeafTextAction {
                objectName: "unlink-cancel"
                label: Bond.cancelLabel
                anchors.verticalCenter: parent.verticalCenter
                onTriggered: unlinking.close()
            }

            ActionButton {
                objectName: "unlink-go"
                label: Bond.unlinkGo
                danger: true
                ready: !Bond.busy
                onTriggered: {
                    unlinking.close()
                    Bond.unlink()
                }
            }
        }

        implicitHeight: unlinkBody.implicitHeight + 16 + 16 + 32 + 16
    }

    RelayDialog {
        id: relay
    }

    /// What to say under a connection that is not one. `Settings` writes the sentence when
    /// the setup is unfinished — it knows which of the address and the key is absent, and
    /// which file it wants; otherwise the server's own refusal is the honest answer.
    function whatIsWrong() {
        if (Settings.missing.length > 0)
            return Settings.missing
        return Health.trouble
    }

    readonly property int margin: Widths.shelfMargin + 12
    readonly property int gap: 28
}
