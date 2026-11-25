// Whom a series is credited to, on its sheet: the people its volumes name, once each. The
// series' own Scénario, which credits the volumes that name nobody, is a field among the
// others, above its Dessin.
//
// Read off the volumes because a series credited with the first volume's writer was frozen on
// one name while its nine volumes had twelve: what the volumes say is shown as theirs, and
// corrected in the sheet of the volume that says it. Where it was read is not said: the
// list shows whom, and « Calculé » or « Écrits à la main » beside it was a second question.

pragma ComponentBehavior: Bound

import QtQuick
import Leaf

Column {
    id: block

    /// `Review.sheet`.
    required property var sheet

    readonly property var credits: block.sheet.credits ?? ({})
    readonly property var everyone: block.credits.all ?? []
    readonly property var words: SheetCaptions.sheetWords
    /// The whole list, unfolded. Folded back on every other sheet.
    property bool unfolded: false
    property string shownId: ""

    objectName: "review-credits"
    visible: block.everyone.length > 0
    spacing: Widths.space.compact
    topPadding: Widths.space.row

    onSheetChanged: {
        if (block.sheet.id !== block.shownId)
            block.unfolded = false
        block.shownId = block.sheet.id ?? ""
    }

    Rectangle {
        width: block.width
        height: 1
        color: Theme.rule
    }

    Item {
        width: block.width
        height: title.implicitHeight

        Text {
            id: title

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: block.words.seriesCredits
            textFormat: Text.PlainText
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(13)
            font.weight: Font.Medium
        }

    }

    Text {
        objectName: "review-credits-people"
        width: block.width
        text: block.unfolded ? block.everyone.join(", ") : (block.credits.computed ?? "")
        textFormat: Text.PlainText
        color: Theme.ink
        font.family: Theme.textFamily
        font.pixelSize: Widths.scale.body(13)
        wrapMode: Text.WordWrap

        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    // Only when some are folded away: three names or fewer are all there already.
    CardAction {
        objectName: "review-credits-unfold"
        visible: block.everyone.length > 3
        label: block.unfolded ? block.words.fold : block.words.showAll
        onTriggered: block.unfolded = !block.unfolded
    }
}
