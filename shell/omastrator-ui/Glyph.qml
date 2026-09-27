import QtQuick
import QtQuick.Shapes
import "Icons.js" as Icons

// One icon from Icons.js, scaled from its 18-point square to `size`.
// Icons have at most three parts.
Item {
  id: root

  property string name: ""
  property color color: "white"
  property real size: 18

  readonly property var partList: Icons.parts(name)

  function part(index) {
    return partList[index] || null
  }

  implicitWidth: size
  implicitHeight: size

  component Part: ShapePath {
    required property var spec
    strokeColor: spec ? root.color : "transparent"
    strokeWidth: spec && spec.width ? spec.width : 1.5
    fillColor: spec && spec.fill ? root.color : "transparent"
    capStyle: ShapePath.RoundCap
    joinStyle: ShapePath.RoundJoin
    PathSvg { path: spec ? spec.d : "M0 0" }
  }

  Shape {
    width: 18
    height: 18
    preferredRendererType: Shape.CurveRenderer
    transform: Scale { xScale: root.size / 18; yScale: root.size / 18 }
    Part { spec: root.part(0) }
    Part { spec: root.part(1) }
    Part { spec: root.part(2) }
  }
}
