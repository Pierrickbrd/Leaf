// The reading orders of a universe, on its sheet: one line each, with what it reads, and
// whether it is the one read when none is chosen or one another order holds.
//
// Shown and not edited here: typed as a field, the orders were a box of JSON on a sheet of
// values, and an order written again inside another one — the same three steps kept in two
// places — said so nowhere. The note under them says it. A line is opened in the order editor,
// by a click or by Enter, as long as the card can still be corrected.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Column {
    id: block

    /// `Review.sheet`.
    required property var sheet

    readonly property var orders: block.sheet.orders ?? []
    readonly property var words: SheetCaptions.sheetWords
    /// Whether a line opens its order: not on a card already on its way.
    readonly property bool openable: block.sheet.editable === true

    /// The line of `orderId` asked to be opened.
    signal opening(string orderId)

    function focusOrder(orderId: string): bool {
        for (let at = 0; at < lines.count; ++at) {
            const line = lines.itemAt(at)
            if (line && line.said.id === orderId) {
                line.forceActiveFocus(Qt.TabFocusReason)
                return true
            }
        }
        return false
    }

    objectName: "review-orders"
    visible: block.orders.length > 0
    spacing: Widths.space.compact
    topPadding: Widths.space.row

    Rectangle {
        width: block.width
        height: 1
        color: Theme.rule
    }

    Text {
        text: block.words.readingOrders
        textFormat: Text.PlainText
        color: Theme.ink
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.body(13)
        font.weight: Font.Medium
    }

    Repeater {
        id: lines

        model: block.orders.length

        delegate: Rectangle {
            id: order

            required property int index
            // As `ReviewRail`'s node: read once more past the end of a list that shrank.
            readonly property var said: block.orders[order.index]
                                        ?? ({ id: "", name: "", line: "", isDefault: false,
                                              subset: false })
            readonly property string badge: order.said.isDefault ? block.words.defaultOrder
                                            : order.said.subset ? block.words.subset : ""

            objectName: "review-order-" + order.said.id
            width: block.width
            height: Math.max(name.implicitHeight + line.implicitHeight, mark.height)
                    + 2 * Widths.space.compact
            radius: Widths.scale.size(10)
            color: pointer.hovered ? Theme.rule : Theme.onPaper
            border.color: Theme.rule
            border.width: 1

            activeFocusOnTab: block.openable

            Accessible.role: block.openable ? Accessible.Button : Accessible.StaticText
            Accessible.name: [order.said.name, order.said.line, order.badge]
                             .filter(one => one.length > 0).join(", ")
            Accessible.onPressAction: order.open()

            function open(): void {
                if (block.openable)
                    block.opening(order.said.id)
            }

            Keys.onPressed: event => {
                if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                        || event.key === Qt.Key_Enter) {
                    order.open()
                    event.accepted = true
                }
            }

            HoverHandler {
                id: pointer

                enabled: block.openable
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                enabled: block.openable
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: order.open()
            }

            FocusRing {
                objectName: order.objectName + "-focus"
                cornerRadius: order.radius
            }

            Text {
                id: name

                objectName: order.objectName + "-name"
                anchors.left: parent.left
                anchors.leftMargin: Widths.space.row
                anchors.right: mark.left
                anchors.rightMargin: Widths.space.row
                anchors.top: parent.top
                anchors.topMargin: Widths.space.compact
                text: order.said.name
                textFormat: Text.PlainText
                color: Theme.ink
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.body(13)
                font.weight: Font.Medium
                elide: Text.ElideRight
            }

            Text {
                id: line

                objectName: order.objectName + "-line"
                anchors.left: name.left
                anchors.right: name.right
                anchors.top: name.bottom
                text: order.said.line
                textFormat: Text.PlainText
                color: Theme.inkSoft
                font.family: Theme.textFamily
                font.pixelSize: Widths.scale.caption(12)
                elide: Text.ElideRight
            }

            // The default is said, as a value said is: `defaultOrder` writes it. A subset is
            // written nowhere — this client works it out from what the other orders read — so
            // it wears what a value worked out wears.
            OriginChip {
                id: mark

                objectName: order.objectName + "-badge"
                anchors.right: parent.right
                anchors.rightMargin: Widths.space.row
                anchors.verticalCenter: parent.verticalCenter
                visible: order.badge.length > 0
                origin: order.said.isDefault ? Review.Said : Review.Guessed
                label: order.badge
            }
        }
    }

    Text {
        objectName: "review-orders-note"
        width: block.width
        visible: text.length > 0
        text: block.sheet.ordersNote ?? ""
        textFormat: Text.PlainText
        color: Theme.inkSoft
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.caption(12)
        lineHeight: 1.3
        wrapMode: Text.WordWrap

        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
}
