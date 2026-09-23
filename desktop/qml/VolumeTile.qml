// One file of an edition, on a cover.
//
// Exactly the shelf's tile applied to a volume: the cover, the name under it, what it weighs
// under that. A series is drawn the same way everywhere, and a volume had no reason to escape
// it — which is why the marks are the shelf's marks too.
//
// **A bar and not a ring.** A cover has a bottom edge to lay a bar along; a line of a list has
// none, and that is the whole of the difference between this and `VolumeRow`. Finished, the
// bar is full *and* a check sits in the far corner: the bar alone said two opposite things,
// never opened and done, by being absent in one case and complete in the other.
//
// Never opened carries neither, and no word either: an empty track drawn on every cover would
// be fifty grey lines on a wall of illustrations.

import QtQuick
import Qt5Compat.GraphicalEffects
import Leaf

Item {
    id: tile

    required property string entryId
    required property string number
    required property string title
    required property string pages
    required property string cover
    required property string fileName
    required property int state
    required property real howFarRead
    /// The edition this belongs to, which its menu commands it through.
    required property string seriesId
    required property real coverWidth
    required property real coverHeight

    readonly property bool missing: state === 3
    readonly property bool finished: state === 2
    readonly property bool started: state === 1
    readonly property bool hovered: pointer.hovered

    signal opened()
    signal reimportAsked()

    objectName: "tile-volume-" + (entryId.length > 0 ? entryId : "missing-" + number)
    width: coverWidth
    height: coverHeight + 6 + 34 + 2 + 16

    Accessible.role: Accessible.ListItem
    Accessible.name: tile.number + " " + tile.title
    Accessible.description: tile.missing ? tile.title : tile.pages

    HoverHandler {
        id: pointer

        enabled: !tile.missing
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        enabled: !tile.missing
        onTapped: tile.opened()
    }

    Item {
        id: frame

        width: tile.coverWidth
        height: tile.coverHeight

        CoverShadow {
            under: tile.entryId
            visible: !tile.missing
        }

        Rectangle {
            id: clipped

            anchors.fill: parent
            radius: Theme.coverRadius
            color: Theme.onPaper
            clip: true
            antialiasing: true
            visible: !tile.missing

            Item {
                id: source

                anchors.fill: parent

                Image {
                    anchors.fill: parent
                    source: tile.cover
                    asynchronous: true
                    cache: true
                    fillMode: Image.PreserveAspectCrop
                }

                // A green cover would swallow the emerald laid on it. The shade is invisible
                // on a dark one and saves the bar on every other.
                LinearGradient {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 15
                    visible: tile.started || tile.finished
                    start: Qt.point(0, 0)
                    end: Qt.point(0, height)
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#00000000" }
                        GradientStop { position: 1.0; color: "#66000000" }
                    }
                }

                Rectangle {
                    objectName: "read-so-far-" + tile.entryId
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    width: parent.width * (tile.finished ? 1 : Math.max(0.04, tile.howFarRead))
                    height: 4
                    visible: tile.started || tile.finished
                    color: Theme.emerald
                }
            }

            ShaderEffectSource {
                id: texture

                sourceItem: source
                hideSource: true
                visible: false
            }

            OpacityMask {
                anchors.fill: parent
                source: texture
                maskSource: Rectangle {
                    width: clipped.width
                    height: clipped.height
                    radius: clipped.radius
                    antialiasing: true
                }
            }
        }

        // Nothing to show, so nothing is shown: a dashed frame in place of a cover, and the
        // word in the alert colour. A hole in a collection is drawn and left alone.
        Canvas {
            anchors.fill: parent
            visible: tile.missing
            antialiasing: true

            onPaint: {
                const context = getContext("2d")
                context.reset()
                context.strokeStyle = Theme.rule
                context.lineWidth = 1
                context.setLineDash([4, 4])
                context.strokeRect(0.5, 0.5, width - 1, height - 1)
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 16
                text: tile.title
                color: Theme.alert
                font.family: Theme.textFamily
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.coverRadius
            color: "transparent"
            border.color: Theme.rule
            border.width: 1
            visible: !tile.missing
            antialiasing: true
        }

        // Far from the bar: two marks on the same edge fought for the room. The bar says
        // « how far » and this says « and it is done », which a fill rounded up to a hundred
        // per cent would not prove.
        Rectangle {
            id: done

            objectName: "finished-" + tile.entryId
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            width: 20
            height: 20
            radius: 99
            visible: tile.finished
            color: Theme.emerald
            antialiasing: true

            Image {
                id: tick

                anchors.centerIn: parent
                width: 14
                height: 14
                source: "assets/icons/check.svg"
                sourceSize.width: 14
                sourceSize.height: 14
                fillMode: Image.PreserveAspectFit
                visible: false
            }

            ColorOverlay {
                anchors.fill: tick
                source: tick
                color: Theme.onEmerald
            }
        }

        // To the left of the check and never over it — the corner is taken, and a button does
        // not cover what it commands.
        CommandMenu {
            objectName: "commands-tile-" + tile.entryId
            anchors.right: parent.right
            anchors.rightMargin: tile.finished ? 32 : 4
            anchors.top: parent.top
            anchors.topMargin: 4
            visible: !tile.missing && (tile.hovered || opened)
            seriesId: tile.seriesId
            entryId: tile.entryId
            fileName: tile.fileName
            label: tile.title
            onReimportAsked: tile.reimportAsked()
        }
    }

    Text {
        id: name

        anchors.left: frame.left
        anchors.right: frame.right
        anchors.top: frame.bottom
        anchors.topMargin: 6
        height: Math.min(implicitHeight, 34)
        text: tile.number + " · " + tile.title
        color: tile.missing ? Theme.inkFaint : Theme.ink
        font.family: Theme.displayFamily
        font.pixelSize: 13
        font.weight: Font.DemiBold
        lineHeightMode: Text.FixedHeight
        lineHeight: 17
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    Text {
        anchors.left: frame.left
        anchors.right: frame.right
        anchors.top: name.bottom
        anchors.topMargin: 2
        height: 16
        text: tile.pages
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: 11
        elide: Text.ElideRight
    }
}
