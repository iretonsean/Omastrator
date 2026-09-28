import QtQuick
import QtQuick.Effects
import "." as O

// A floating surface: the panel tone, no border on a tone-separated theme, a
// soft shadow under it and a 1 px highlight along its top. `depth` sizes the
// shadow: 1 for panels, less for small cards near other things.
Rectangle {
  id: root
  property real depth: 1

  radius: O.Theme.radiusPanel
  color: O.Theme.surface1
  border.color: O.Theme.edge
  border.width: O.Theme.edgeWidth

  RectangularShadow {
    z: -1
    anchors.fill: parent
    radius: root.radius
    offset.y: Math.round(4 + 12 * root.depth)
    blur: Math.round(12 + 28 * root.depth)
    color: Qt.rgba(0, 0, 0, 0.5)
    visible: root.depth > 0 && root.visible
  }

  Rectangle {
    anchors { top: parent.top; left: parent.left; right: parent.right; leftMargin: root.radius; rightMargin: root.radius }
    height: 1
    color: O.Theme.highlight
  }
}
