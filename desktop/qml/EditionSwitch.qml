// « Albums · 2 éditions ⌄ », and the menu it opens.
//
// A pill and a floating list, which is what the design draws: two rows the width of the page
// stacked under the header pushed everything down by eighty pixels to say what a menu says
// without moving anything at all.
//
// The one being read stays in the list, with a check — taking it out would make a reader
// remember where they came from. And the count is under **both** names, the current one
// included: it is often the only thing that really tells two editions apart, so it cannot
// disappear at the moment one is chosen.

import QtQuick
import QtQuick.Controls
import Leaf

Item {
    id: switcher

    /// `[{ seriesId, name, detail, cover, here }]`, as the page hands them over.
    required property var editions
    required property string label

    signal chosen(string seriesId)

    /// Closes, then says which. Both halves run here rather than in the row that was tapped:
    /// choosing re-points the page, which rebuilds this list, so that row is torn down in the
    /// middle of its own handler — and anything it looked up afterwards, `menu` included,
    /// came back undefined. « Cannot call method 'close' of undefined », four times a click.
    function take(seriesId) {
        menu.close()
        switcher.chosen(seriesId)
    }

    objectName: "edition-switch"
    implicitWidth: pill.width
    implicitHeight: 24
    visible: opacity > 0

    // The editions of a work are a second question, and it cannot be asked before the first
    // is answered — the work is what that answer names. So the pill cannot land with the
    // rest of the header, and what it can do is arrive rather than appear. The same thing
    // the design asks of a cover that changes: the old one stays until the new one is ready.
    opacity: label.length > 0 ? 1 : 0

    Behavior on opacity {
        NumberAnimation {
            duration: 140
            easing.type: Easing.OutQuad
        }
    }

    Rectangle {
        id: pill

        height: 24
        width: word.implicitWidth + chevron.width + 22
        radius: height / 2
        color: pointer.hovered || menu.opened ? Theme.rule : Theme.onPaper

        HoverHandler {
            id: pointer

            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            onTapped: menu.opened ? menu.close() : menu.open()
        }

        Text {
            id: word

            x: 11
            anchors.verticalCenter: parent.verticalCenter
            text: switcher.label
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 13
        }

        Glyph {
            id: chevron

            anchors.left: word.right
            anchors.leftMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            side: 16
            source: "assets/icons/expand_more.svg"
            tint: Theme.inkFaint
            // On the whole thing, so the mark and its colour turn together. Written out, the
            // rotation had to be held in step by hand across two items.
            rotation: menu.opened ? 180 : 0
        }
    }

    Popup {
        id: menu

        objectName: "editions-menu"
        y: pill.height + 5
        width: 264
        padding: 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        background: Rectangle {
            radius: 12
            color: Theme.surface
            border.color: Theme.rule
            border.width: 1

            CardLift { level: CardLift.Menu }
        }

        contentItem: Column {
            spacing: 2

            Repeater {
                model: switcher.editions

                Rectangle {
                    id: option

                    required property var modelData

                    objectName: "edition-" + modelData.seriesId
                    width: 256
                    height: 48
                    radius: 9
                    color: modelData.here || hover.hovered ? Theme.onPaper : "transparent"

                    HoverHandler {
                        id: hover

                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        onTapped: switcher.take(option.modelData.seriesId)
                    }

                    // Flush against the left edge: a cover is what one recognises first, and
                    // it has no business being indented behind nothing.
                    Item {
                        id: art

                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 28
                        height: 42

                        CoverShadow {
                            under: "edition-" + option.modelData.seriesId
                        }

                        RoundedCover {
                            anchors.fill: parent
                            source: option.modelData.cover
                            radius: 4
                        }

                        // Outside the cover and not inside it: what the rounding masks is the
                        // picture, and a hairline baked into that texture is a hairline cut
                        // twice.
                        CoverSkin { radius: 4 }
                    }

                    Column {
                        anchors.left: art.right
                        anchors.leftMargin: 8
                        anchors.right: tick.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0

                        Text {
                            width: parent.width
                            text: option.modelData.name
                            color: Theme.ink
                            font.family: Theme.textFamily
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: option.modelData.detail
                            color: Theme.inkFaint
                            font.family: Theme.textFamily
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }

                    Glyph {
                        id: tick

                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        side: 18
                        source: "assets/icons/check.svg"
                        tint: Theme.emerald
                        visible: option.modelData.here
                    }
                }
            }
        }
    }
}
