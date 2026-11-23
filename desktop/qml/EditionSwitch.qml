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
    implicitHeight: pill.height
    visible: opacity > 0

    /// A stop of the page's walk — see `KeyWalk`. Enter opens the list, the arrows move in it
    /// and Enter again takes the edition they are on.
    readonly property bool keyStop: true

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                || event.key === Qt.Key_Enter) {
            menu.open()
            event.accepted = true
        }
    }

    FocusRing {
        objectName: "edition-switch-focus"
        anchors.fill: pill
        cornerRadius: pill.radius
    }

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

        // From its word: a literal 24 round a word that follows the desktop's text size cut
        // it at half as large again.
        height: Math.max(Widths.scale.fit(24), word.implicitHeight + Widths.scale.fit(6))
        width: word.implicitWidth + chevron.width + Widths.scale.fit(22)
        radius: height / 2
        color: pointer.hovered || menu.opened ? Theme.rule : Theme.onPaper

        HoverHandler {
            id: pointer

            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: menu.opened ? menu.close() : menu.open()
        }

        Text {
            id: word

            x: Widths.scale.fit(11)
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: switcher.label
            color: Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
        }

        Glyph {
            id: chevron

            anchors.left: word.right
            anchors.leftMargin: Widths.space.tight
            anchors.verticalCenter: parent.verticalCenter
            side: Widths.scale.icon(16)
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
        // `compact` from its pill and inside, as every menu: its padding was 4, half the sort
        // menu's and the filters' 8, for the same surface (spacing audit, 23/10/2026).
        y: pill.height + Widths.space.compact
        width: Widths.scale.column(264)
        padding: Widths.space.compact
        margins: Widths.space.gutter
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        // Taken, so that the arrows reach the list and Escape closes it before the page.
        focus: true

        /// The edition the keys are on, starting on the one being read.
        property int pointed: -1

        onOpened: {
            menu.pointed = 0
            for (let i = 0; i < switcher.editions.length; ++i) {
                if (switcher.editions[i].here)
                    menu.pointed = i
            }
        }

        background: Rectangle {
            radius: Theme.buttonRadius
            color: Theme.surface
            border.color: Theme.rule
            border.width: 1

            CardLift { level: CardLift.Menu }
        }

        contentItem: Column {
            spacing: Widths.space.tight
            focus: true

            /// The keys moved the edition they are on: their ring is shown. It moves inside the
            /// list and not from one item to the next, which is what lights a ring anywhere
            /// else — opened with a click, the list was walked with the arrows and no ring.
            function keysMoved() {
                const cursor = switcher.Window.window ? switcher.Window.window.navigationCursor
                                                      : null
                if (cursor)
                    cursor.pointerTookOver = false
            }

            Keys.onPressed: event => {
                const last = switcher.editions.length - 1
                if (event.key === Qt.Key_Down || event.key === Qt.Key_Right
                        || (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier))) {
                    menu.pointed = Math.min(last, menu.pointed + 1)
                    keysMoved()
                    event.accepted = true
                } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Left
                           || event.key === Qt.Key_Backtab || event.key === Qt.Key_Tab) {
                    menu.pointed = Math.max(0, menu.pointed - 1)
                    keysMoved()
                    event.accepted = true
                } else if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                           || event.key === Qt.Key_Enter) {
                    if (menu.pointed >= 0 && menu.pointed <= last)
                        switcher.take(switcher.editions[menu.pointed].seriesId)
                    event.accepted = true
                }
            }

            Repeater {
                model: switcher.editions.length

                Rectangle {
                    id: option

                    required property int index
                    readonly property var modelData: switcher.editions[index]
                                                     ?? ({ "seriesId": "", "here": false })

                    objectName: "edition-" + modelData.seriesId
                    width: menu.availableWidth
                    height: Math.max(Widths.scale.size(48),
                                     art.height + Widths.scale.size(6),
                                     said.implicitHeight + Widths.space.compact)
                    radius: 9
                    color: modelData.here || hover.hovered ? Theme.onPaper : "transparent"

                    FocusRing {
                        objectName: option.objectName + "-focus"
                        cornerRadius: option.radius
                        focused: menu.pointed === option.index
                    }

                    HoverHandler {
                        id: hover

                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: switcher.take(option.modelData.seriesId)
                    }

                    // Flush against the left edge: a cover is what one recognises first, and
                    // it has no business being indented behind nothing.
                    Item {
                        id: art

                        x: Widths.scale.size(4)
                        anchors.verticalCenter: parent.verticalCenter
                        width: Widths.scale.size(28)
                        height: Widths.scale.size(42)

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
                        id: said

                        anchors.left: art.right
                        anchors.leftMargin: Widths.space.label
                        anchors.right: tick.left
                        anchors.rightMargin: Widths.scale.room(6)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0

                        Text {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: option.modelData.name
                            color: Theme.ink
                            font.family: Theme.textFamily
                            font.pixelSize: Widths.scale.body(14)
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: option.modelData.detail
                            color: Theme.inkFaint
                            font.family: Theme.textFamily
                            font.pixelSize: Widths.scale.caption(12)
                            elide: Text.ElideRight
                        }
                    }

                    Glyph {
                        id: tick

                        anchors.right: parent.right
                        anchors.rightMargin: Widths.space.compact
                        anchors.verticalCenter: parent.verticalCenter
                        side: Widths.scale.icon(18)
                        source: "assets/icons/check.svg"
                        tint: Theme.emerald
                        visible: option.modelData.here
                    }
                }
            }
        }
    }
}
