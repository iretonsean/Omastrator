import QtQuick
import qs.Commons
import "OverlayLogic.js" as Logic

// Desktop Look's gap handle (docs/ANYWHERE.md, phase 4): the gap to the right
// of the window the bar is on. Dragging it widens or narrows the gap; letting
// go previews the new gap on the desktop through `omastrator design look`.
// Nothing is written until Save in Desktop Look.
Rectangle {
  id: handle

  required property var handleData
  required property color accent
  signal dropped(var args)

  property real dragged: 0
  readonly property int value: handleData ? Logic.gapAfterDrag(handleData, dragged) : 0

  visible: handleData !== null
  x: handleData ? handleData.x - (width - Math.max(0, handleData.width + dragged)) / 2 : 0
  y: handleData ? handleData.y : 0
  // Never thinner than a finger's worth to grab.
  width: handleData ? Math.max(10, handleData.width + dragged) : 0
  height: handleData ? handleData.height : 0
  color: Util.alpha(accent, grab.containsMouse || grab.pressed ? 0.35 : 0.18)
  border.color: accent
  border.width: 1

  Rectangle {
    anchors.centerIn: parent
    width: label.implicitWidth + Style.space(10)
    height: label.implicitHeight + Style.space(4)
    radius: 3
    color: handle.accent
    visible: grab.containsMouse || grab.pressed

    Text {
      id: label
      anchors.centerIn: parent
      text: handle.handleData ? handle.handleData.label + " " + handle.value : ""
      color: Color.popups.background
      font.family: Style.font.family
      font.pixelSize: Style.font.caption
      font.weight: Font.DemiBold
    }
  }

  MouseArea {
    id: grab
    anchors.fill: parent
    hoverEnabled: true
    cursorShape: Qt.SizeHorCursor
    property real startX: 0
    onPressed: function (mouse) {
      startX = mapToItem(null, mouse.x, 0).x
      handle.dragged = 0
    }
    onPositionChanged: function (mouse) {
      if (pressed)
        handle.dragged = mapToItem(null, mouse.x, 0).x - startX
    }
    onReleased: {
      handle.dropped(Logic.gapArgs(handle.handleData, handle.value))
      handle.dragged = 0
    }
  }
}
