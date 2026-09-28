import QtQuick
import QtQuick.Effects
import "." as O

// Where the pointer is: the lift tone, a top highlight and a small shadow,
// faded in and out. The accent is for what is chosen (see `chosen`), which
// takes the soft accent instead.
Item {
  id: root
  property bool hot: false
  property bool chosen: false
  property bool resting: false
  property int radius: O.Theme.radiusRow

  // A chosen fill sits under the lift.
  Rectangle {
    anchors.fill: parent
    radius: root.radius
    color: root.chosen ? O.Theme.accentSoft : O.Theme.surface2
    opacity: root.chosen || root.resting ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: O.Theme.snap; easing.type: Easing.OutCubic } }
    Behavior on color { ColorAnimation { duration: O.Theme.snap } }
  }

  Item {
    anchors.fill: parent
    opacity: root.hot && !root.chosen ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: O.Theme.snap; easing.type: Easing.OutCubic } }

    RectangularShadow {
      anchors.fill: parent
      radius: root.radius
      offset.y: 2
      blur: 8
      color: Qt.rgba(0, 0, 0, 0.4)
    }
    Rectangle {
      anchors.fill: parent
      radius: root.radius
      color: O.Theme.lift
      Rectangle {
        anchors { top: parent.top; left: parent.left; right: parent.right; leftMargin: root.radius; rightMargin: root.radius }
        height: 1
        color: O.Theme.highlight
      }
    }
  }
}
