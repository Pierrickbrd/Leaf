// One series: a header that says who it is, and three tabs under it.
//
// **Nothing empties to refill.** The models keep what they hold until an answer arrives, so
// this file has no "clear then load" anywhere: it draws what is there, and the skeleton is
// shown at the one moment there is nothing to keep — the first opening from the shelf.
//
// The resume band sits above the tab bar, so changing tab does not move it: it belongs to the
// series and not to a tab. When nothing has been started it is not there at all — not an
// empty band saying so — and the tabs come up against the header, four lines higher.

import QtQuick
import Leaf

Item {
    id: view

    /// Replaceable so the QML can be exercised against a small model. The application never
    /// assigns it: it gets the one singleton for the whole run.
    property var page: Series
    property var volumes: Entries

    readonly property int volumesTab: 0
    readonly property int descriptionTab: 1

    objectName: "series-view"
    clip: true

    property int current: view.volumesTab

    Flickable {
        id: scroll

        anchors.fill: parent
        anchors.leftMargin: Widths.shelfMargin
        anchors.rightMargin: Widths.shelfMargin
        contentWidth: width
        contentHeight: stack.implicitHeight + 32
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: stack

            y: 16
            width: parent.width
            spacing: 14

            SeriesSkeleton {
                width: parent.width
                height: 168
                // The one screen with nothing to keep. Changing edition or opening another
                // series never shows it: there is always a page to hold on to.
                visible: !view.page.available && view.page.loading
            }

            SeriesHeader {
                width: parent.width
                visible: view.page.available
                universe: view.page.universe
                work: view.page.work
                edition: view.page.edition
                cover: view.page.cover
                makers: view.page.makers
                weights: view.page.weights
                genres: view.page.genres
                editionsLabel: view.page.editionsLabel
                onUniverseAsked: Shelf.filterBy({ "universe": [view.page.universe] })
                onEditionsAsked: switcher.visible = !switcher.visible
            }

            // Opens on the spot rather than taking the page away: this is a change of
            // edition, not a navigation.
            Column {
                id: switcher

                width: parent.width
                spacing: 6
                visible: false

                Repeater {
                    model: view.page.editions

                    Rectangle {
                        required property var modelData

                        width: switcher.width
                        height: 40
                        radius: Theme.cardRadius
                        color: modelData.here ? Theme.emeraldWash : Theme.surface

                        TapHandler {
                            onTapped: {
                                view.page.point(parent.modelData.identifier)
                                view.volumes.point(parent.modelData.identifier, [])
                                switcher.visible = false
                            }
                        }

                        Column {
                            x: 14
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1

                            Text {
                                text: parent.parent.modelData.name
                                color: Theme.ink
                                font.family: Theme.displayFamily
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }

                            // Under the name and on both, the one being read included: it is
                            // often the only thing that really tells two editions apart, so
                            // it cannot disappear at the moment one is chosen.
                            Text {
                                text: parent.parent.modelData.count
                                color: Theme.inkFaint
                                font.family: Theme.textFamily
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            ResumeBand {
                width: parent.width
                // Absent, not empty: a grey band announcing « nothing started » would say the
                // same thing and take the room of four volumes.
                visible: Resume.available && Resume.seriesId === view.page.identifier
            }

            SeriesTabs {
                width: parent.width
                visible: view.page.available
                current: view.current
                asGrid: Preferences.volumesAsGrid
                query: view.volumes.query
                onChosen: which => view.current = which
                onViewToggled: Preferences.volumesAsGrid = !Preferences.volumesAsGrid
                onSearched: text => view.volumes.searchFor(text)
            }

            // The list. The page scrolls, not a box inside it — one scroll, like the shelf,
            // and the cut is the bottom of the screen rather than a box with white under it.
            Column {
                width: parent.width
                spacing: 0
                visible: view.current === view.volumesTab

                Repeater {
                    model: view.volumes

                    // The roles are declared by `VolumeRow` itself and injected here by the
                    // repeater. Re-declaring them is a duplicate name, and a delegate that
                    // fails to be created fails quietly — the list simply stays empty.
                    VolumeRow {
                        width: parent.width
                        neverReadWord: SeriesCaptions.neverRead
                    }
                }

                Text {
                    width: parent.width
                    text: SeriesCaptions.nothingFound
                    visible: SeriesCaptions.narrowedToNothing
                    color: Theme.inkSoft
                    font.family: Theme.textFamily
                    font.pixelSize: 12
                    topPadding: 12
                }
            }

            SeriesDescription {
                width: parent.width
                visible: view.current === view.descriptionTab
                summary: view.page.summary
                credits: view.page.credits
                nature: view.page.nature
                holding: view.page.holding
            }
        }
    }
}
