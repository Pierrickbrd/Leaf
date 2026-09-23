// One series cover and its two lines, shared by the shelf and both search views.

import QtQuick
import Qt5Compat.GraphicalEffects
import Leaf

Item {
    id: tile

    required property int index
    required property string seriesId
    required property string name
    required property string work
    required property string cover
    required property string medium
    required property string volumes
    required property bool inProgress
    required property real howFarRead
    required property real cellWidth
    required property real cellHeight
    required property real coverWidth
    required property real coverHeight
    required property real viewportClearance
    required property bool selected

    signal pointerEntered()
    signal pointerExited()
    signal opened()

    objectName: "tile-" + seriesId
    width: cellWidth
    height: cellHeight

    Accessible.role: Accessible.ListItem
    Accessible.name: name
    Accessible.description: volumes
    Accessible.focusable: true
    Accessible.focused: selected

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        onEntered: tile.pointerEntered()
        onExited: tile.pointerExited()
    }

    // Beside the hover area rather than inside it: that one takes no button on purpose, so
    // that a stationary pointer never steals the emphasis a key just gave somewhere else.
    TapHandler {
        onTapped: tile.opened()
    }

    Item {
        id: coverFrame

        objectName: "cover-frame-" + tile.seriesId
        x: tile.viewportClearance
        y: tile.viewportClearance
        width: tile.coverWidth
        height: tile.coverHeight

        CoverShadow {
            under: tile.seriesId
        }

        Rectangle {
            id: clippedCover

            objectName: "clipped-cover-" + tile.seriesId
            anchors.fill: parent
            radius: Theme.coverRadius
            color: Theme.onPaper
            clip: true
            antialiasing: true

            Item {
                id: coverSource
                anchors.fill: parent

                Image {
                    id: coverImage

                    objectName: "cover-" + tile.seriesId
                    anchors.fill: parent
                    source: tile.cover
                    asynchronous: true
                    cache: true
                    fillMode: Image.PreserveAspectCrop
                }

                // A mark, not a measure. It used to span the cover's whole width in the
                // emerald the band above draws its progress bar with — the same colour, the
                // same four pixels, one of them saying « how far » and the other « started
                // ». Read as a bar it claimed a series was finished the moment it was
                // opened. A short segment cannot be mistaken for a fraction of anything,
                // and the shelf has no fraction to show: nothing in a series row says how
                // many of its volumes have been read.
                // How far through the series, drawn as the fraction it is.
                //
                // It spanned the cover's whole width whenever a series was merely started,
                // which read as finished — the emerald, the four pixels and the full width
                // of the band's own progress bar above, saying « started » where that one
                // says « how far ». It could not say more: the shelf had `readStatus` and
                // nothing else, and « how far » is a number the server now sends.
                Rectangle {
                    objectName: "in-progress-" + tile.seriesId
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 4
                    color: Theme.rule
                    opacity: tile.inProgress ? 1 : 0

                    Rectangle {
                        objectName: "read-so-far-" + tile.seriesId
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * tile.howFarRead
                        color: Theme.emerald
                    }
                }
            }

            ShaderEffectSource {
                id: coverTexture
                sourceItem: coverSource
                hideSource: true
                visible: false
            }

            OpacityMask {
                anchors.fill: parent
                source: coverTexture
                maskSource: Rectangle {
                    width: clippedCover.width
                    height: clippedCover.height
                    radius: clippedCover.radius
                    antialiasing: true
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.coverRadius
            color: "transparent"
            border.color: Theme.rule
            border.width: 1
            antialiasing: true
        }

        FocusRing {
            objectName: "focus-" + tile.seriesId
            visible: tile.selected
        }
    }

    Text {
        id: title

        objectName: "title-" + tile.seriesId
        anchors.left: coverFrame.left
        anchors.right: coverFrame.right
        anchors.top: coverFrame.bottom
        anchors.topMargin: 6
        height: Math.min(implicitHeight, 40)
        text: tile.name
        color: Theme.ink
        font.family: Theme.displayFamily
        font.pixelSize: 16
        font.weight: Font.DemiBold
        lineHeightMode: Text.FixedHeight
        lineHeight: 20
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    Text {
        objectName: "volumes-" + tile.seriesId
        anchors.left: coverFrame.left
        anchors.right: coverFrame.right
        anchors.top: title.bottom
        anchors.topMargin: 2
        height: 18
        text: tile.volumes
        color: Theme.inkFaint
        font.family: Theme.textFamily
        font.pixelSize: 14
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
