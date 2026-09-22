// One line of an import's tree: its chevron, its sign, its name, and what becomes of it.
//
// The indent says which level of the model one is at — that is information, not decoration:
// two editions under a work read as two editions *of* that work, where four lines on one
// margin read as four unrelated things.
//
// **Two lines, one separator, one left edge.** The name, then everything else about it —
// « Série · déjà là · 21 tomes · 2,2 Gio ». It was three stacked lines, then two columns with
// the level right-aligned: down a twenty-one volume series that read as two unrelated lists,
// and the right-hand one had no left edge to line up against.
//
// The sign is aligned on the name's own line rather than centred on the block, for the same
// reason it exists at all: it marks the thing the name names, and the line underneath belongs
// to neither.

import QtQuick
import Leaf

Item {
    id: line

    required property var node

    signal folded()

    readonly property int depth: node.depth === undefined ? 0 : node.depth
    /// Mirrors `Imports::Tone`. Numbers here because QML sees an enum across a model role as
    /// an int, and the model is the one place the states are sorted into tones — comparing
    /// French strings in a `.qml` file is what `Words` exists to prevent.
    readonly property int quiet: 0
    readonly property int attention: 3

    // Named from outside, by the card that draws it — never here. Every other child of
    // `ImportRow` reads `line.objectName + "-…"`, and a node that named itself let two
    // cards open together answer the same `import-node-root` to whichever a test asked
    // first.
    width: parent ? parent.width : 0
    implicitHeight: words.implicitHeight + 12
    height: implicitHeight

    Accessible.role: Accessible.TreeItem
    Accessible.name: line.node.name

    TapHandler {
        enabled: line.node.expandable === true
        onTapped: line.folded()
    }

    HoverHandler {
        id: pointer

        enabled: line.node.expandable === true
        cursorShape: Qt.PointingHandCursor
    }

    // A hovered row lights faintly, so that a tree four levels deep says which line a click
    // would open. `rule` in both themes, the step `FilterChip` uses for the same reason.
    //
    // It spans the whole row and the content sits eight pixels inside it on both sides —
    // the chevron used to start at the very edge and the text to end at it, so the lit
    // background read as a band drawn through the words rather than around them.
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 1
        anchors.bottomMargin: 1
        radius: 6
        color: pointer.hovered ? Theme.rule : "transparent"
        visible: line.node.expandable === true
    }

    // The chevron is drawn: it expresses a control's state — open, closed — never a thing,
    // so it follows `LeafCheck`'s rule and not `LevelMark`'s.
    Canvas {
        id: chevron

        x: line.depth * 20 + 8
        // Anchored to the words and not to the line: the line is taller than its first row
        // whenever there is a second one under it, and a fixed `y` put the chevron a little
        // higher on a node with a weight than on one without.
        anchors.top: words.top
        anchors.topMargin: 3
        width: 10
        height: 10
        visible: line.node.expandable === true
        rotation: line.node.expanded === true ? 90 : 0
        transformOrigin: Item.Center

        Behavior on rotation {
            NumberAnimation { duration: 90 }
        }

        onPaint: {
            const ink = getContext("2d")
            ink.reset()
            ink.strokeStyle = pointer.hovered ? Theme.ink : Theme.inkSoft
            ink.lineWidth = 1.5
            ink.lineCap = "round"
            ink.beginPath()
            ink.moveTo(3, 1)
            ink.lineTo(7, 5)
            ink.lineTo(3, 9)
            ink.stroke()
        }

        Connections {
            target: Theme

            function onChanged() {
                chevron.requestPaint()
            }
        }
    }

    /// The card owns the decision, not the line: a node knows what it is, and `ImportRow`
    /// knows which row of the model it belongs to.
    signal replacing(bool wanted)
    signal declaring(string path, bool wanted)

    LevelMark {
        id: mark

        x: line.depth * 20 + 24
        anchors.top: words.top
        anchors.topMargin: -1
        level: line.node.level
        size: 20
        tint: pointer.hovered ? Theme.ink : Theme.inkSoft
    }

    Column {
        id: words

        anchors.left: mark.right
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: 8
        // Centred, so the space above the first line and under the last are the same. Held
        // at the top with a two-pixel margin, they were two above and eight below — which
        // the hovered background made plain, its outline sitting against the name and a
        // finger's width from the weight.
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Text {
            id: name

            objectName: line.objectName + "-name"
            width: parent.width
            text: line.node.name
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: 13
            font.weight: line.depth === 0 ? Font.Medium : Font.Normal
            elide: Text.ElideMiddle
        }

        // What the library already holds where this volume would land, and the box that
        // decides. Clear by default: dropping a folder on a library is not a request to
        // overwrite it, and six volumes that could be replaced are six decisions — the same
        // argument the boxes under `moves` already make, one line at a time.
        LeafCheck {
            objectName: line.objectName + "-replace"
            visible: line.node.replaceable === true
            width: parent.width
            label: line.node.present === undefined ? "" : line.node.present
            checked: line.node.replacing === true
            onToggled: wanted => line.replacing(wanted)
        }

        // The same decision one floor up: a `work.json` carries a title and a summary
        // somebody may have edited through the API, and dropping the folder again is not a
        // request to undo that. Its own box because it is its own question — a series can
        // have every volume already in place and still disagree about what it is called.
        LeafCheck {
            objectName: line.objectName + "-declare"
            visible: line.node.redeclarable === true
            width: parent.width
            label: line.node.present === undefined ? "" : line.node.present
            checked: line.node.declaring === true
            onToggled: wanted => line.declaring(line.node.declarationPath === undefined
                                                ? "" : line.node.declarationPath, wanted)
        }

        // The level, what becomes of it, and what it weighs — one line, one separator.
        //
        // Its colour is the model's `tone` and never the state's spelling: a tone per state
        // would be seven colours, and `FilterChip` argues that a colour per value makes every
        // colour mean nothing. The whole line takes it rather than one word inside it, so
        // that a volume about to be overwritten is visible while running an eye down
        // twenty-one of them — which is the one moment this screen has to be read quickly.
        Text {
            objectName: line.objectName + "-says"
            width: parent.width
            visible: text.length > 0
            text: CardCaptions.nodeLine(line.node.level,
                                          line.node.state === undefined ? "" : line.node.state,
                                          line.node.holds === undefined ? "" : line.node.holds)
            color: line.node.tone === line.attention ? Theme.alert
                   : line.node.tone === line.quiet ? Theme.inkFaint
                                                   : Theme.inkSoft
            font.family: Theme.textFamily
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }
}
