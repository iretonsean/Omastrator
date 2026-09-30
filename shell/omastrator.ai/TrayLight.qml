import QtQuick
import qs.Commons
import qs.Ui
import "../omastrator-ui" as O
import "../omastrator.design/OverlayLogic.js" as Logic

// The tray light (docs/OS-SUITE.md): one glyph in the bar with four states.
// A click brings Omastrator forward with the Ask field focused; there is never a second AI menu.
BarWidget {
  id: root

  O.Status { id: status }

  readonly property string state: {
    if (status.value("error", "")) return "error"
    if (status.value("waiting", "")) return "working"
    if (status.value("ready", false)) return "ready"
    return "idle"
  }

  readonly property string statusText: {
    if (root.state === "error") return status.value("error", "")
    if (root.state === "working") return status.value("waiting", "")
    if (root.state === "ready") return Logic.readyTooltip(status.status)
    return status.connected ? "Omastrator AI: nothing running" : "Omastrator isn't installed or can't start"
  }

  readonly property color lightColor: root.state === "error" ? Color.urgent
    : root.state === "idle" ? Util.alpha(bar ? bar.barForeground : Color.foreground, 0.75)
    : Color.accent

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    tooltipText: root.statusText
    iconComponent: Item {
      O.Glyph {
        id: glyph
        anchors.centerIn: parent
        name: "ai"
        size: Style.bar.iconCanvas
        color: root.lightColor

        // Working breathes; nothing else moves.
        SequentialAnimation on opacity {
          running: root.state === "working"
          loops: Animation.Infinite
          alwaysRunToEnd: true
          NumberAnimation { from: 1; to: 0.35; duration: 700; easing.type: Easing.InOutSine }
          NumberAnimation { from: 0.35; to: 1; duration: 700; easing.type: Easing.InOutSine }
        }
      }

      // Results ready: a dot at the corner.
      Rectangle {
        visible: root.state === "ready"
        width: Math.max(4, Style.space(5))
        height: width
        radius: width / 2
        color: Color.accent
        anchors.right: glyph.right
        anchors.top: glyph.top
        anchors.rightMargin: -width / 3
        anchors.topMargin: -width / 3
      }
    }
    onPressed: function (mouseButton) { status.run(["island", "ask"]) }
  }
}
