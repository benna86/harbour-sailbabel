import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
    property string choice

    Column {
        width: parent.width

        DialogHeader {
        title: "Erase which dictionary?"
        }

        ListView {
            id: list
            width: parent.width; height: parent.height
            model: dictionary.langs
            delegate: ListItem {
                          width: parent.width
                          contentHeight: label.height+Theme.paddingLarge+Theme.paddingSmall
                          Item {
                              id: item
                              height: label.height+Theme.paddingLarge
                              width: parent.width
                              Label {
                                  id: label
                                  text: modelData
                                  x: Theme.horizontalPageMargin
                                  width: parent.width-2*x
                                  anchors.top: item.top
                                  anchors.topMargin: 0.35*Theme.paddingLarge
                                  truncationMode: TruncationMode.Fade
                              }
                          }
                          MouseArea {
                              anchors.fill: parent
                              onClicked: list.currentIndex = index
                          }
            }
            highlight: Rectangle { color: "lightsteelblue"; radius: 5 }
        }
    }

    onDone: {
        if (result == DialogResult.Accepted) {
            choice = list.model[list.currentIndex]
        }
    }
}
