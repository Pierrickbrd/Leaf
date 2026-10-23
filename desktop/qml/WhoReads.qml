// « Qui lit ? » — the whole window, the way a video service asks it: one tile per profile, a
// ring around the one reading now, and a tile that adds one.
//
// It decides only which door a choice goes through. Whether the change is allowed is the
// server's answer; whether a code is asked first is the table of the design, read from what
// `Profiles` last heard.

import QtQuick
import QtQuick.Controls
import Leaf

FocusScope {
    id: whoReads

    /// Leaving needs the code: a restricted profile, or a device whose restricted profile was
    /// deleted under it. No way out but a profile, so no cross, no « Ajouter », no settings.
    readonly property bool locked: Profiles.isRestricted || Profiles.held
    /// Every route but this screen's is refused until somebody is chosen, so there is no
    /// shelf to go back to.
    readonly property bool mustChoose: Profiles.choosing
    /// Opened at launch on a principal this device does not keep, behind the code: anybody
    /// may be sitting there, so no way out but a profile.
    readonly property bool unproven: Profiles.unproven
    readonly property bool closable: !locked && !mustChoose && !unproven
    property bool keep: false

    /// The code window (9b): `target` is a profile of `Profiles.list`, or null with `add`.
    signal codeNeeded(var target, string mode)
    /// The profile window (9c), opened on a new profile.
    signal profileDialogAsked(string mode, var profile)
    signal manageAsked()
    signal dismissed()

    /// Whether the table of the design asks the code before reading as `target`. Only a device
    /// that retains the principal reaches it freely, as the server rules: one merely left on
    /// it reopened there for anyone.
    function needsCode(target) {
        if (whoReads.locked)
            return true
        return target.kind === "principal" && Profiles.codeScope === "everything"
                && !(Profiles.isPrincipal && Profiles.kept)
    }

    /// Only a profile free to leave may be kept: a device that keeps a restricted profile is
    /// the principal's decision, made in Appareils, never this box's.
    function keeps(target) {
        return whoReads.keep && (target.kind === "principal" || target.kind === "reader")
    }

    function choose(target) {
        // The profile already reading: nothing to change, unless it is now to be kept —
        // which `Profiles` does not count as a switch, so nothing is reloaded for it. Not the
        // principal on a seat that does not retain it: staying there is the code's to allow.
        const stays = target.kind !== "principal" || !whoReads.needsCode(target)
        if (target.current && !whoReads.mustChoose && stays) {
            if (whoReads.keeps(target))
                Profiles.switchTo(target.id, "", true)
            whoReads.dismissed()
            return
        }
        if (whoReads.needsCode(target)) {
            whoReads.codeNeeded(target, "switch")
            return
        }
        Profiles.switchTo(target.id, "", whoReads.keeps(target))
    }

    /// From a reader, adding is the principal's power, which the code guards when the
    /// principal chose so; from the principal it simply opens.
    function add() {
        if (!Profiles.isPrincipal && Profiles.codeScope === "everything")
            whoReads.codeNeeded(null, "add")
        else
            whoReads.profileDialogAsked("new", null)
    }

    objectName: "who-reads"
    onVisibleChanged: {
        if (visible) {
            whoReads.keep = false
            whoReads.forceActiveFocus()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.paper
    }

    // Nothing under it answers: the shelf is still there, and a click through the paper
    // opened a series nobody could see.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onPressed: mouse => mouse.accepted = true
        onWheel: wheel => wheel.accepted = true
    }

    Rectangle {
        id: top

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 58
        color: Theme.surface

        Image {
            id: mark

            anchors.left: parent.left
            anchors.leftMargin: Widths.shelfMargin
            anchors.verticalCenter: parent.verticalCenter
            width: 36
            height: 36
            source: Theme.dark ? "assets/logos/leaf-mark-dark.svg"
                               : "assets/logos/leaf-mark-light.svg"
            sourceSize: Qt.size(36, 36)
        }

        Text {
            anchors.left: mark.right
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: "Leaf"
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: 25
            font.weight: Font.Bold
        }

        BarButton {
            objectName: "who-reads-close"
            anchors.right: parent.right
            anchors.rightMargin: Widths.shelfMargin
            anchors.verticalCenter: parent.verticalCenter
            visible: whoReads.closable
            activeFocusOnTab: visible
            source: "assets/icons/close.svg"
            label: Profiles.words.closeWhoReads
            onTriggered: whoReads.dismissed()
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.rule
            opacity: Theme.dark ? 0.7 : 0.45
        }
    }

    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: top.height / 2
        spacing: 40

        Text {
            objectName: "who-reads-title"
            anchors.horizontalCenter: parent.horizontalCenter
            text: Profiles.words.whoReads
            color: Theme.ink
            font.family: Theme.displayFamily
            font.pixelSize: 44
            font.weight: Font.Bold
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 28

            Repeater {
                model: Profiles.list

                delegate: Column {
                    id: tile

                    required property var modelData

                    objectName: "who-reads-tile-" + modelData.id
                    width: 140
                    spacing: 20
                    activeFocusOnTab: true

                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.name
                    Accessible.onPressAction: whoReads.choose(tile.modelData)

                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                                || event.key === Qt.Key_Enter) {
                            whoReads.choose(tile.modelData)
                            event.accepted = true
                        }
                    }

                    Item {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 132
                        height: 132

                        Rectangle {
                            objectName: "who-reads-ring-" + tile.modelData.id
                            visible: tile.modelData.current
                            anchors.centerIn: parent
                            width: 148
                            height: 148
                            radius: 36
                            color: "transparent"
                            border.color: face.color
                            border.width: 3
                            antialiasing: true
                        }

                        ProfileSquare {
                            id: face

                            objectName: "who-reads-square-" + tile.modelData.id
                            profile: tile.modelData
                            side: 132
                            radius: 28
                        }

                        FocusRing {
                            cornerRadius: 36
                            focused: tile.activeFocus
                        }

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }

                        TapHandler {
                            gesturePolicy: TapHandler.ReleaseWithinBounds
                            onTapped: whoReads.choose(tile.modelData)
                        }
                    }

                    Text {
                        objectName: "who-reads-name-" + tile.modelData.id
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        text: tile.modelData.name
                        // A reader names itself, and every profile sees this screen: « <img
                        // src=…> » as a name was drawn as markup and fetched on each opening.
                        textFormat: Text.PlainText
                        color: Theme.ink
                        font.family: Theme.textFamily
                        font.pixelSize: 16
                        font.weight: Font.Medium
                    }

                    // The age a restricted profile reads up to, the one fact about it that
                    // decides what it will see. The principal's crown says the rest.
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: tile.modelData.kind === "restricted" && tile.modelData.maxAge > 0
                              ? Profiles.saySeriesUpToYears(tile.modelData.maxAge)
                              : (tile.modelData.kind === "principal" ? "" : tile.modelData.note)
                        color: Theme.inkSoft
                        font.family: Theme.textFamily
                        font.pixelSize: 13
                    }
                }
            }

            Column {
                id: addTile

                objectName: "who-reads-add"
                visible: whoReads.closable
                width: 140
                spacing: 12
                activeFocusOnTab: visible

                Accessible.role: Accessible.Button
                Accessible.name: Profiles.words.addOne
                Accessible.onPressAction: whoReads.add()

                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                            || event.key === Qt.Key_Enter) {
                        whoReads.add()
                        event.accepted = true
                    }
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 132
                    height: 132
                    radius: 28
                    color: Theme.surface
                    border.color: Theme.rule
                    border.width: 1
                    antialiasing: true

                    Glyph {
                        anchors.centerIn: parent
                        side: 44
                        source: "assets/icons/add.svg"
                        tint: Theme.inkSoft
                    }

                    FocusRing {
                        cornerRadius: 28
                        focused: addTile.activeFocus
                    }

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: whoReads.add()
                    }
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: Profiles.words.addOne
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 16
                    font.weight: Font.Medium
                }
            }
        }

        LeafCheck {
            objectName: "who-reads-keep"
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !whoReads.locked
            width: 230
            activeFocusOnTab: visible
            label: Profiles.words.keepOnThisDevice
            checked: whoReads.keep
            onToggled: wanted => whoReads.keep = wanted
        }

        LeafTextAction {
            objectName: "who-reads-manage"
            anchors.horizontalCenter: parent.horizontalCenter
            visible: whoReads.closable
            label: Profiles.words.manageProfiles
            onTriggered: whoReads.manageAsked()
        }

        Text {
            objectName: "who-reads-needs-code"
            anchors.horizontalCenter: parent.horizontalCenter
            visible: whoReads.locked
            text: Profiles.words.switchNeedsCode
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 14
        }

        ActionButton {
            objectName: "who-reads-back"
            anchors.horizontalCenter: parent.horizontalCenter
            visible: whoReads.locked && !whoReads.mustChoose
            label: Profiles.words.backToShelf
            onTriggered: whoReads.dismissed()
        }
    }
}
