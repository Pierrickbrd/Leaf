// Réglages › Profils: who reads here, and what each of them may do.
//
// The principal sees every profile and the rights of each reader, and changes a right with
// one click — a box that waited for a « Save » would be a box that looked changed and was
// not. A reader sees their own row and the name of whoever manages the rest: hiding the
// others without a word reads as profiles that vanished.

import QtQuick
import Leaf

Item {
    id: section

    /// The left column's width, the gap between the two, and whether they stand side by side —
    /// the settings screen's own layout, handed down so the six tabs share it.
    property real side: width
    property real gap: 28
    property bool sideBySide: false

    signal profileDialogAsked(string mode, var profile)
    signal removalAsked(var profile)

    readonly property var shown: Profiles.isPrincipal ? Profiles.list
                                                      : Profiles.list.filter(one => one.current)
    readonly property var principal: Profiles.list.find(one => one.kind === "principal") ?? null
    /// The rights are the principal's to give, so only the principal sees them; and only once
    /// there is someone else to give them to.
    readonly property bool grants: Profiles.isPrincipal && Profiles.several

    height: sideBySide ? Math.max(left.height, right.height)
                       : left.height + (grants ? gap + right.height : 0)

    // The devices name where each profile is kept; only the principal is given them. Later,
    // for the reason `DevicesSection` gives.
    onVisibleChanged: if (visible && Profiles.isPrincipal) Qt.callLater(Profiles.devices)
    Component.onCompleted: if (visible && Profiles.isPrincipal) Qt.callLater(Profiles.devices)

    /// The optional parts of a profile's line: « vous », and every device that opens on it.
    /// This device is read from the seat, which a reader has too; the others from the list of
    /// devices, which only the principal is given.
    function marks(profile) {
        const out = []
        if (profile.current)
            out.push(Profiles.words.youMark)
        if (profile.current && Profiles.kept)
            out.push(Profiles.words.retainedHere)
        for (const device of Profiles.deviceList) {
            if (!device.here && device.kept && device.profile === profile.id)
                out.push(Profiles.sayRetainedOn(device.name))
        }
        return out
    }

    Column {
        id: left

        width: section.side
        spacing: section.gap

        SettingsCard {
            objectName: "settings-profiles"
            title: Profiles.isPrincipal ? Profiles.words.profilesTab : Profiles.words.yourProfile

            Column {
                width: parent.width
                spacing: 4

                Repeater {
                    model: section.shown

                    delegate: Item {
                        id: row

                        required property var modelData

                        objectName: "profile-row-" + modelData.id
                        width: parent.width
                        height: 56

                        ProfileSquare {
                            id: square

                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            profile: row.modelData
                            side: 40
                        }

                        Column {
                            anchors.left: square.right
                            anchors.leftMargin: 14
                            anchors.right: actions.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                objectName: row.objectName + "-name"
                                width: parent.width
                                text: row.modelData.name
                                textFormat: Text.PlainText
                                color: Theme.ink
                                font.family: Theme.textFamily
                                font.pixelSize: 14
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }

                            Text {
                                objectName: row.objectName + "-note"
                                width: parent.width
                                text: Profiles.sayProfileLine(row.modelData,
                                                              section.marks(row.modelData))
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

                            // One's own row is « Votre profil », the principal's included: the
                            // name, the colour and, for the principal, the code.
                            IconButton {
                                objectName: row.objectName + "-edit"
                                glyph: "edit"
                                label: Profiles.sayEditProfile(row.modelData.name)
                                onTriggered: section.profileDialogAsked(
                                                 row.modelData.current ? "self" : "edit",
                                                 row.modelData)
                            }

                            // Never the principal, whom nothing replaces, and never one's own:
                            // the server refuses both, and a bin that is refused is a bin that
                            // lies.
                            IconButton {
                                objectName: row.objectName + "-remove"
                                visible: Profiles.isPrincipal && row.modelData.kind !== "principal"
                                         && !row.modelData.current
                                glyph: "delete"
                                label: Profiles.sayEraseProfileButton(row.modelData.name)
                                onTriggered: section.removalAsked(row.modelData)
                            }
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: 40
                    visible: Profiles.isPrincipal

                    LeafTextAction {
                        objectName: "profiles-add"
                        x: -8
                        anchors.verticalCenter: parent.verticalCenter
                        label: Profiles.words.addAProfile
                        onTriggered: section.profileDialogAsked("new", null)
                    }
                }

                SettingsNote {
                    objectName: "profiles-erase-note"
                    width: parent.width
                    visible: Profiles.isPrincipal && Profiles.several
                    text: Profiles.words.eraseExplained
                }

                SettingsNote {
                    objectName: "profiles-managed"
                    width: parent.width
                    visible: !Profiles.isPrincipal && section.principal !== null
                    text: Profiles.sayManagedByPrincipal(section.principal?.name ?? "")
                }
            }
        }
    }

    Column {
        id: right

        x: section.sideBySide ? section.side + section.gap : 0
        y: section.sideBySide ? 0 : left.height + section.gap
        width: section.width - x
        spacing: section.gap
        visible: section.grants

        SettingsCard {
            objectName: "settings-rights"
            title: Profiles.words.whatEachMayDo

            Column {
                width: parent.width
                spacing: 14

                Repeater {
                    model: Profiles.list.filter(one => one.kind !== "principal")

                    delegate: Column {
                        id: entry

                        required property var modelData
                        readonly property var rights: modelData.rights

                        width: parent.width
                        spacing: 6

                        /// One right changed, the other two sent as they stand: the server
                        /// takes the three together.
                        function grant(which, wanted) {
                            const sent = { import: entry.rights.import, edit: entry.rights.edit,
                                           erase: entry.rights.erase }
                            sent[which] = wanted
                            Profiles.change(entry.modelData.id, { rights: sent })
                        }

                        Text {
                            width: parent.width
                            text: entry.modelData.name
                            textFormat: Text.PlainText
                            color: Theme.ink
                            font.family: Theme.textFamily
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }

                        Flow {
                            width: parent.width
                            spacing: 18
                            visible: entry.modelData.kind === "reader"

                            Repeater {
                                model: [{ right: "import", label: Profiles.words.mayImport },
                                        { right: "edit", label: Profiles.words.mayEdit },
                                        { right: "erase", label: Profiles.words.mayErase }]

                                delegate: LeafCheck {
                                    required property var modelData

                                    objectName: "rights-" + entry.modelData.id + "-"
                                                + modelData.right
                                    width: 110
                                    label: modelData.label
                                    checked: entry.rights[modelData.right]
                                    enabled: !Profiles.busy
                                    onToggled: wanted => entry.grant(modelData.right, wanted)
                                }
                            }
                        }

                        // A restricted profile has no boxes at all: it reads, whatever is
                        // ticked, and a box that changes nothing is a box that lies.
                        SettingsNote {
                            objectName: "rights-" + entry.modelData.id + "-only-reads"
                            width: parent.width
                            visible: entry.modelData.kind !== "reader"
                            text: Profiles.sayOnlyReads(entry.modelData.name)
                        }
                    }
                }

                SettingsNote {
                    width: parent.width
                    text: Profiles.words.rightsExplained
                    muted: true
                }
            }
        }
    }
}
