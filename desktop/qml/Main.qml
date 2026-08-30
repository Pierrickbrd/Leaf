import QtQuick
import QtQuick.Controls

ApplicationWindow {
    width: 1100
    height: 760
    visible: true
    title: qsTr("Leaf")

    Label {
        anchors.centerIn: parent
        text: qsTr("Leaf")
        font.pixelSize: 48
    }
}
