// How a cover is chosen in an editing window: an image from the reader's own disk, and, for a
// volume, one of its own pages, for a series, one of its volumes. One « Changer… » opens the
// ways, so the window shows one control for the cover and the choices are read in the menu it
// opens, keys and pointer alike. Nothing is sent from here — the window asks what was chosen
// (`cover()`, `page()`) when it saves, as it does for every other field, so that « Annuler »
// leaves the cover as it was.
//
// A page, a volume and an image exclude each other: the last one chosen is the one kept, which
// is also what the server does with them (the image sent later replaces the page or the volume,
// a page or a volume chosen later outranks the image). « Revenir par défaut » is the third state, not the absence of the
// other two, and it is offered only where something has been chosen to go back from.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import Leaf

Row {
    id: choice

    /// Names the controls for whoever looks them up: `<prefix>-pick-image` and so on.
    required property string prefix
    /// The editing words (`Editing.words`).
    required property var words
    /// The cover as it is drawn now.
    required property string current
    /// What going back means, in words: « Revenir au tome 1 » for a series.
    required property string revertLabel
    /// What the cover is when nothing was chosen, in words.
    required property string defaultStatus
    /// Whether something chosen earlier is already kept: an image the server holds, a page the
    /// volume names.
    property bool kept: false
    /// The page of the volume that is kept, from 1, for the status line; 0 when none.
    property int keptPage: 0
    /// A volume offers its pages; a series has none of its own.
    property bool pages: false
    property string pagesUrl: ""
    property int pageCount: 0
    /// A series offers its volumes, `[{ number, label, cover }]`; `null` for a volume, which
    /// has pages and no volumes.
    property var volumes: null
    /// The `number` of the volume the series names as its cover, `undefined` when none.
    property var keptVolume: undefined

    /// What was chosen in this window and not yet sent.
    property string pickedFile: ""
    property int pickedPage: -1
    /// The `number` of a volume, `null` when none: a volume can be numbered 0.
    property var pickedVolume: null
    property bool reverted: false

    readonly property bool volumesOffered: choice.volumes !== null && choice.volumes.length > 1
    readonly property bool pagesOffered: choice.pages && choice.pageCount > 1
    readonly property bool picked: choice.pickedFile.length > 0 || choice.pickedPage >= 0
                                   || choice.pickedVolume !== null
    readonly property bool changed: choice.picked || choice.reverted
    readonly property bool canRevert: !choice.reverted && (choice.picked || choice.kept)

    function reset() {
        choice.pickedFile = ""
        choice.pickedPage = -1
        choice.pickedVolume = null
        choice.reverted = false
    }

    /// The place in `volumes` of the volume numbered `number`, −1 when the series has no such.
    function volumeAt(number) {
        if (choice.volumes === null || number === undefined || number === null)
            return -1
        return choice.volumes.findIndex(one => one.number === number)
    }

    /// For `Commands.editSeries` and `editEntry`: what to do with an image.
    function cover() {
        if (choice.pickedFile.length > 0)
            return { "file": choice.pickedFile }
        if (choice.reverted)
            return { "clear": true }
        return ({})
    }

    function chooseFile(url) {
        choice.reset()
        choice.pickedFile = url
    }

    function choosePage(index) {
        choice.reset()
        choice.pickedPage = index
    }

    /// By its place in `volumes`, as the grid says it. The volume the series already names is
    /// not a change: choosing it again leaves nothing to send.
    function chooseVolume(index) {
        choice.reset()
        const number = choice.volumes[index].number
        if (number !== choice.keptVolume)
            choice.pickedVolume = number
    }

    function nameOf(url) {
        const parts = url.split("/")
        return decodeURIComponent(parts[parts.length - 1])
    }

    readonly property string status: {
        if (choice.reverted)
            return choice.defaultStatus
        if (choice.pickedFile.length > 0)
            return choice.nameOf(choice.pickedFile)
        if (choice.pickedPage >= 0)
            return choice.words.coverChosenPage.replace("%1", choice.pickedPage + 1)
        if (choice.pickedVolume !== null)
            return choice.volumes[choice.volumeAt(choice.pickedVolume)].label
        if (choice.keptPage > 0)
            return choice.words.coverChosenPage.replace("%1", choice.keptPage)
        if (choice.volumeAt(choice.keptVolume) >= 0)
            return choice.volumes[choice.volumeAt(choice.keptVolume)].label
        return choice.kept ? choice.words.coverChosenImage : choice.defaultStatus
    }

    spacing: Widths.space.compact + Widths.space.compact

    Item {
        width: Widths.scale.size(70)
        height: Widths.scale.size(105)

        RoundedCover {
            objectName: choice.prefix + "-preview"
            anchors.fill: parent
            radius: Widths.scale.size(Theme.coverRadius)
            source: choice.pickedFile.length > 0 ? choice.pickedFile
                    : choice.pickedPage >= 0 ? choice.pagesUrl + choice.pickedPage + "?width=240"
                    : choice.pickedVolume !== null
                      ? choice.volumes[choice.volumeAt(choice.pickedVolume)].cover + "?width=240"
                    : choice.current
        }

        CoverSkin {
            radius: Widths.scale.size(Theme.coverRadius)
        }
    }

    Column {
        width: choice.width - Widths.scale.size(70) - choice.spacing
        spacing: Widths.space.compact

        Text {
            textFormat: Text.PlainText
            text: choice.words.cover
            color: Theme.inkFaint
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.caption(12)
            font.weight: Font.Medium
        }

        Text {
            objectName: choice.prefix + "-status"
            width: parent.width
            text: choice.status
            color: Theme.ink
            font.family: Theme.textFamily
            font.pixelSize: Widths.scale.body(14)
            elide: Text.ElideMiddle
            textFormat: Text.PlainText
        }

        // The pill the client already has for a secondary command (`Pill`), with what a pill
        // lacks to be reached by the keys: a stop, a ring, and Space or Return to press it.
        Pill {
            id: change

            objectName: choice.prefix + "-change"
            label: choice.words.changeCover
            height: Widths.scale.size(32)
            padding: Widths.scale.size(16)
            activeFocusOnTab: true
            onPressed: ways.opened ? ways.close() : ways.open()

            Accessible.role: Accessible.Button
            Accessible.name: change.label
            Accessible.focusable: true
            Accessible.onPressAction: change.pressed()

            Keys.onPressed: event => {
                if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                        || event.key === Qt.Key_Enter) {
                    if (!event.isAutoRepeat)
                        change.pressed()
                    event.accepted = true
                }
            }

            FocusRing {
                objectName: choice.prefix + "-change-focus"
                cornerRadius: change.height / 2
            }

            LeafMenu {
                id: ways

                objectName: choice.prefix + "-ways"
                parent: change
                y: change.height + Widths.space.compact

                // `enabled: visible`, as the menu of the three dots: what is not shown cannot
                // be reached with the arrows.
                LeafMenuItem {
                    objectName: choice.prefix + "-pick-volume"
                    visible: choice.volumesOffered
                    enabled: visible
                    height: visible ? implicitHeight : 0
                    text: choice.words.chooseVolume
                    onTriggered: volumePager.open()
                }
                LeafMenuItem {
                    objectName: choice.prefix + "-pick-page"
                    visible: choice.pagesOffered
                    enabled: visible
                    height: visible ? implicitHeight : 0
                    text: choice.words.choosePage
                    onTriggered: pager.open()
                }
                LeafMenuItem {
                    objectName: choice.prefix + "-pick-image"
                    text: choice.words.chooseImage
                    onTriggered: finder.open()
                }
                LeafMenuItem {
                    objectName: choice.prefix + "-revert"
                    visible: choice.canRevert
                    enabled: visible
                    height: visible ? implicitHeight : 0
                    text: choice.revertLabel
                    onTriggered: {
                        choice.reset()
                        choice.reverted = true
                    }
                }
            }
        }
    }

    FileDialog {
        id: finder

        objectName: choice.prefix + "-image-dialog"
        title: choice.words.chooseImageTitle
        nameFilters: [choice.words.imageFiles]
        onAccepted: choice.chooseFile(finder.selectedFile.toString())
    }

    PagePicker {
        id: pager

        objectName: choice.prefix + "-pages"
        words: choice.words
        pagesUrl: choice.pagesUrl
        pageCount: choice.pageCount
        onChosen: index => choice.choosePage(index)
    }

    PagePicker {
        id: volumePager

        objectName: choice.prefix + "-volumes"
        words: choice.words
        pagesUrl: ""
        pageCount: 0
        covers: choice.volumes
        heading: choice.words.chooseVolumeTitle
        onChosen: index => choice.chooseVolume(index)
    }
}
