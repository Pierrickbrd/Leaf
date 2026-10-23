// What you can change, and what merely is.
//
// Two columns, and the split is not decorative: the left is what a reader acts on, the
// right is what the library is doing. Reading order puts the thing you came to do first,
// and a state you cannot change has no business competing with it. Below the wide band
// they stack, the actions still first.
//
// **What is not here.** Where the key sits — a reader does not need it, and this is a client
// for reading comics. It appears under the connection and only when it is broken, which is
// the one moment it helps rather than clutters; the versions wait on À propos. The address is
// the exception, on the library tab: it is where this device was linked, and the one thing
// a person may have to correct there. The name of this device, linking another and unlinking
// are on the devices tab, with the other devices; and every tab is there only for a profile
// that manages what it holds (`Settings.sections`).

import QtQuick
import QtQuick.Controls
import Leaf

Item {
    id: page

    /// The tab asked for, by name. Shown only while this profile has it: a switch to a profile
    /// that does not manage what was open falls back to the first tab rather than to nothing.
    property string section: "general"
    readonly property var sections: Settings.sections(Profiles.active.kind ?? "")
    readonly property string current: sectionIndex(section) >= 0 ? section : "general"
    /// The address being typed in, in place of the line that shows it.
    property bool moving: false

    readonly property bool sideBySide: Widths.band === Widths.Wide

    /// The profile window and the removal question belong to the window, which opens them
    /// the same way for « Qui lit ? »; this screen only asks.
    signal profileDialogAsked(string mode, var profile)
    signal profileRemovalAsked(var profile)

    objectName: "settings-view"
    clip: true

    Component.onCompleted: {
        // Réglages › Profils, from « Qui lit ? »: the tab is handed over with the destination.
        if (Navigation.parameters.section)
            page.section = Navigation.parameters.section
        Health.ask()
        Scan.ask()
    }

    function sectionIndex(name) {
        return page.sections.findIndex(tab => tab.name === name)
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
        const tab = tabs.tabAt(forward ? 0 : page.sections.length - 1)
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
        choices: page.sections
        currentIndex: page.sectionIndex(page.current)
        onSelected: index => page.section = page.sections[index].name
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
            height: Math.max(acted.height, observed.height,
                             profilesSection.visible ? profilesSection.height : 0,
                             devicesSection.visible ? devicesSection.height : 0,
                             aboutSection.visible ? aboutSection.height : 0)
                    + (page.sideBySide ? 0 : observed.height > 0 ? page.gap : 0)

            // Halves, because that is what two columns means. The left one holds what
            // you act on and the right what the library is doing; neither earns more of
            // the page than the other.
            readonly property real side: page.sideBySide
                                         ? Math.round((width - page.gap) / 2) : width

            ProfilesSection {
                id: profilesSection

                width: columns.width
                side: columns.side
                gap: page.gap
                sideBySide: page.sideBySide
                visible: page.current === "profiles"
                onProfileDialogAsked: (mode, profile) => page.profileDialogAsked(mode, profile)
                onRemovalAsked: profile => page.profileRemovalAsked(profile)
            }

            DevicesSection {
                id: devicesSection

                width: columns.width
                side: columns.side
                gap: page.gap
                sideBySide: page.sideBySide
                visible: page.current === "devices"
            }

            AboutSection {
                id: aboutSection

                width: columns.width
                side: columns.side
                gap: page.gap
                sideBySide: page.sideBySide
                visible: page.current === "about"
            }

            Column {
                id: acted

                objectName: "settings-actions"
                width: columns.side
                spacing: page.gap

                // First in the column, above the cards: it is what the click that sent the
                // person here was answered.
                SettingsNote {
                    objectName: "settings-already-linked"
                    visible: page.current === "library" && Linking.notice.length > 0
                    text: Linking.notice
                }

                SettingsCard {
                    objectName: "settings-appearance"
                    visible: page.current === "general"
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
                    visible: page.current === "notifications"
                    title: Preferences.warningsTitle

                    Item {
                        width: parent.width
                        height: 18

                        Text {
                            objectName: "warns-event-column"
                            x: 0
                            text: Preferences.eventColumn
                            color: Theme.inkFaint
                            font.family: Theme.textFamily
                            font.pixelSize: 10
                            font.capitalization: Font.AllUppercase
                        }

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

                // Where the library is, and the one thing a person may have to correct about
                // it. This device — its name, linking another, unlinking — is the Appareils
                // tab's: what the library is and what this machine is are two questions.
                SettingsCard {
                    objectName: "settings-bond"
                    visible: page.current === "library"
                    title: Bond.title

                    SettingsStatus {
                        objectName: "bond-address"
                        width: parent.width
                        icon: Health.reachable ? "dns" : "cloud_off"
                        // The address only when the library does not answer, which is the one
                        // moment it helps: « http://127.0.0.1:9099 » told a reader with a
                        // working library nothing, whoever they were, and the tab holds the
                        // way to change it just below.
                        label: Health.reachable ? Health.connected : Bond.address
                        detail: Health.reachable ? Health.holds
                                                 : (Health.asking ? "" : page.whatIsWrong())
                        alarming: !Health.reachable && !Health.asking
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

                    SettingsNote {
                        objectName: "bond-sentence"
                        width: parent.width
                        visible: Bond.sentence.length > 0
                        text: Bond.sentence
                        alarming: !Bond.busy
                    }

                    // Back by the action's own inset, so its words line up with the labels
                    // above rather than with the hover behind them.
                    LeafTextAction {
                        objectName: "bond-move"
                        x: -8
                        label: Bond.moveLabel
                        onTriggered: {
                            moveField.text = Bond.address
                            page.moving = !page.moving
                            if (page.moving)
                                moveField.forceActiveFocus()
                        }
                    }
                }

                SettingsCard {
                    objectName: "settings-scan"
                    visible: page.current === "library"
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
                    visible: page.current === "notifications"
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
                    visible: page.current === "general"
                    title: Health.title

                    SettingsStatus {
                        objectName: "connection"
                        width: parent.width
                        icon: Health.reachable ? "cloud_done" : "cloud_off"
                        label: Health.connected
                        // The address and what the setup is missing, and only when it is:
                        // a reader with a working library has no use for either.
                        // A restricted profile is told what it can see, not what the library
                        // holds: the server already counts under its ceiling.
                        detail: !Health.reachable ? page.whatIsWrong()
                                : Profiles.isRestricted ? Profiles.saySeriesForYou(Health.library)
                                : Health.holds
                        alarming: !Health.reachable && !Health.asking
                    }
                }

                SettingsCard {
                    objectName: "settings-found"
                    visible: page.current === "library" && Scan.counts.length > 0
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
                    model: page.current === "library" ? Scan.findings : []

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
