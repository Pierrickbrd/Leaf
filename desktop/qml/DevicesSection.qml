// Réglages › Appareils: this device, what it opens on, and every other device that reads
// from this library. The principal's alone — the server refuses the list to anyone else.
//
// « Cet appareil » is the block the Bibliothèque tab held when there were no profiles,
// moved whole and with its rules: on a library nobody else reaches, neither linking another
// device nor unlinking, and one sentence in their place.

import QtQuick
import QtQuick.Controls
import Leaf

Item {
    id: section

    /// The settings screen's two columns, handed down — see `ProfilesSection`.
    property real side: width
    property real gap: 28
    property bool sideBySide: false
    /// The name being typed in, in place of the line that shows it.
    property bool renaming: false

    readonly property var here: Profiles.deviceList.find(one => one.here) ?? null
    readonly property var others: Profiles.deviceList.filter(one => !one.here)

    height: sideBySide ? Math.max(left.height, right.height) : left.height + gap + right.height

    // Asked whenever the tab is shown: a device linked or removed elsewhere since the last
    // look is the one this list exists to show. Later, not now: asking says « busy » at once,
    // which redraws the tabs this visibility is being worked out from.
    onVisibleChanged: if (visible) Qt.callLater(Profiles.devices)
    Component.onCompleted: if (visible) Qt.callLater(Profiles.devices)

    function nameOf(profileId) {
        return Profiles.list.find(one => one.id === profileId)?.name ?? ""
    }

    Column {
        id: left

        width: section.side
        spacing: section.gap

        SettingsCard {
            objectName: "settings-this-device"
            title: Profiles.words.thisDevice

            Item {
                objectName: "bond-name"
                width: parent.width
                height: 30
                visible: !section.renaming

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
                        textFormat: Text.PlainText
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
                            section.renaming = true
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
                visible: section.renaming
                onAccepted: {
                    Bond.rename(text)
                    section.renaming = false
                }
                Keys.onEscapePressed: section.renaming = false
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

            // Nobody reaches this library but the machine it runs on: neither relaying
            // nor unlinking, and one plain sentence in their place.
            SettingsNote {
                objectName: "bond-only-here"
                width: parent.width
                visible: Health.onlyHere.length > 0
                text: Health.onlyHere
            }

            SettingsNote {
                objectName: "device-sentence"
                width: parent.width
                visible: Bond.sentence.length > 0
                text: Bond.sentence
                alarming: !Bond.busy
            }

            // Back by the action's own inset, so its words line up with the labels above
            // rather than with the hover behind them.
            LeafTextAction {
                objectName: "bond-unlink"
                x: -8
                visible: Health.unlinks
                label: Bond.unlinkLabel
                alarming: true
                onTriggered: unlinking.open()
            }
        }

        // What Leaf opens on, here. « Demander qui lit » is a device that keeps no profile;
        // a profile is one that keeps it — a restricted one included, which only the principal
        // may decide and only here.
        SettingsCard {
            objectName: "settings-opening"
            visible: section.here !== null
            title: Profiles.words.atOpening

            Text {
                width: parent.width
                text: Profiles.words.opensOnThisComputer
                color: Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }

            SettingsChoice {
                objectName: "opening"
                width: parent.width
                options: [{ value: 0, label: Profiles.words.askWhoReads, icon: "refresh" }]
                         .concat(Profiles.list.map((one, index) => ({
                             value: index + 1,
                             label: one.name,
                             icon: one.kind === "principal" || one.kind === "reader"
                                   ? "bookmark" : "lock"
                         })))
                chosen: !section.here || !section.here.kept
                        ? 0 : 1 + Profiles.list.findIndex(one => one.id === section.here.profile)
                onPicked: value => value === 0
                          ? Profiles.placeDevice(section.here.id, section.here.profile, false)
                          : Profiles.placeDevice(section.here.id, Profiles.list[value - 1].id, true)
            }
        }
    }

    Column {
        id: right

        x: section.sideBySide ? section.side + section.gap : 0
        y: section.sideBySide ? 0 : left.height + section.gap
        width: section.width - x
        spacing: section.gap

        SettingsCard {
            objectName: "settings-other-devices"
            visible: section.others.length > 0
            title: Profiles.words.otherDevices

            Column {
                width: parent.width
                spacing: 4

                Repeater {
                    model: section.others

                    delegate: Item {
                        id: device

                        required property var modelData

                        objectName: "device-row-" + modelData.id
                        width: parent.width
                        height: 52

                        Column {
                            anchors.left: parent.left
                            anchors.right: actions.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                objectName: device.objectName + "-name"
                                width: parent.width
                                text: device.modelData.name
                                textFormat: Text.PlainText
                                color: Theme.ink
                                font.family: Theme.textFamily
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }

                            Text {
                                objectName: device.objectName + "-opens"
                                width: parent.width
                                text: device.modelData.kept
                                      ? Profiles.sayOpensOn(section.nameOf(device.modelData.profile))
                                      : Profiles.words.asksWhoReadsThere
                                textFormat: Text.PlainText
                                color: Theme.inkSoft
                                font.family: Theme.textFamily
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                        }

                        Row {
                            id: actions

                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            // The pencil chooses what that device opens on; only the device
                            // one is sitting at is renamed.
                            IconButton {
                                id: choose

                                objectName: device.objectName + "-choose"
                                glyph: "edit"
                                label: Profiles.words.chooseItsProfile
                                popup: profileMenu
                                held: profileMenu.opened

                                LeafMenu {
                                    id: profileMenu

                                    objectName: device.objectName + "-menu"
                                    y: choose.height + 4

                                    LeafMenuItem {
                                        text: Profiles.words.askWhoReads
                                        checkable: true
                                        checked: !device.modelData.kept
                                        onTriggered: Profiles.placeDevice(device.modelData.id,
                                                                          device.modelData.profile,
                                                                          false)
                                    }

                                    Instantiator {
                                        model: Profiles.list

                                        delegate: LeafMenuItem {
                                            required property var modelData

                                            objectName: device.objectName + "-on-" + modelData.id
                                            text: modelData.name
                                            checkable: true
                                            checked: device.modelData.kept
                                                     && device.modelData.profile === modelData.id
                                            onTriggered: Profiles.placeDevice(device.modelData.id,
                                                                              modelData.id, true)
                                        }

                                        onObjectAdded: (index, object) =>
                                            profileMenu.insertItem(index + 1, object)
                                        onObjectRemoved: (index, object) =>
                                            profileMenu.removeItem(object)
                                    }
                                }
                            }

                            IconButton {
                                objectName: device.objectName + "-remove"
                                glyph: "delete"
                                label: Profiles.words.removeDevice
                                onTriggered: removal.ask(device.modelData)
                            }
                        }
                    }
                }

                SettingsNote {
                    width: parent.width
                    text: Profiles.words.devicesExplained
                    muted: true
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

    // « Retirer la tablette ? » Asked first because its key stops working with the answer: a
    // device removed by a slip has to be linked again from scratch.
    Popup {
        id: removal

        property var device: ({})

        function ask(target) {
            removal.device = target
            removal.open()
        }

        objectName: "device-removal"
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

        contentItem: Item {
            objectName: "device-removal-body"
            implicitHeight: removalText.height + 34 + removalActions.height + 18

            Column {
                id: removalText

                x: 20
                y: 18
                width: parent.width - 40
                spacing: 12

                Text {
                    objectName: "device-removal-title"
                    width: parent.width
                    text: Profiles.sayRemoveDevice(removal.device.name ?? "")
                    textFormat: Text.PlainText
                    color: Theme.ink
                    font.family: Theme.displayFamily
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    text: Profiles.words.removeDeviceConsequence
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 13
                    lineHeight: 1.35
                    wrapMode: Text.Wrap
                }
            }

            Row {
                id: removalActions

                anchors.right: removalText.right
                anchors.top: removalText.bottom
                anchors.topMargin: 18
                spacing: 8

                LeafTextAction {
                    objectName: "device-removal-cancel"
                    anchors.verticalCenter: parent.verticalCenter
                    label: Profiles.words.cancel
                    onTriggered: removal.close()
                }

                ActionButton {
                    objectName: "device-removal-confirm"
                    label: Profiles.words.removeDevice
                    danger: true
                    onTriggered: {
                        removal.close()
                        Profiles.removeDevice(removal.device.id)
                    }
                }
            }
        }
    }
}
