// What you can change, and what merely is.
//
// Two columns, and the split is not decorative: the left is what a reader acts on, the
// right is what the library is doing. Reading order puts the thing you came to do first,
// and a state you cannot change has no business competing with it. Below the wide band
// they stack, the actions still first.
//
// **What is not here.** The server's address, its version, where the key sits — a reader
// does not need them, and this is a client for reading comics. They appear under the
// connection and only when it is broken, which is the one moment they help rather than
// clutter. `Settings.h` says read, never written, and that rule is about the deployment;
// how this application looks on this machine is the reader's own business and is written.

import QtQuick
import QtQuick.Controls
import Leaf

Item {
    id: page

    readonly property int general: 0
    readonly property int library: 1
    property int section: general

    readonly property bool sideBySide: Widths.band === Widths.Wide

    objectName: "settings-view"
    clip: true

    Component.onCompleted: {
        Health.ask()
        Scan.ask()
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
