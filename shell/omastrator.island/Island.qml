import QtQuick
import Quickshell
import Quickshell.Hyprland
import Quickshell.Wayland
import qs.Commons
import "../omastrator-ui" as O

// The Omastrator island (docs/OS-SUITE.md): a pill centred under the bar on
// the focused monitor. It rests on the mode glyph, expands to the mode's
// tools, and briefly shows activity. It holds only what is true about the
// session; tools that depend on the selection stay on the canvas.
Item {
  id: root

  property var shell
  property var manifest
  property string omarchyPath

  O.Status {
    id: status
    onChanged: function (prev, next) { root.noticeChange(prev, next) }
  }

  readonly property string mode: status.value("mode", "normal")
  readonly property bool expanded: status.value("expanded", false)
  readonly property bool running: status.value("running", false)
  readonly property string tool: pendingTool !== "" ? pendingTool : status.value("tool", "select")
  readonly property var labelsSeen: status.value("labelsSeen", [])

  // A click shows at once; the stream confirms it a moment later.
  property string pendingTool: ""
  onToolChanged: if (pendingTool !== "" && status.value("tool", "") === pendingTool) pendingTool = ""
  Timer { id: pendingExpiry; interval: 3000; onTriggered: root.pendingTool = "" }

  readonly property var modeNames: ({ normal: "Normal", draw: "Draw", capture: "Capture", ai: "AI", live: "Live" })
  readonly property var modeTips: ({
    normal: "Normal: the computer as usual",
    draw: "Draw: Omastrator's canvas tools",
    capture: "Capture: colour, screenshots and SVG from anywhere on screen",
    ai: "AI: generate, edit and roast with your agent",
    live: "Live: edit a web page in the browser"
  })

  // What each mode's expanded row holds. `action` runs `omastrator <action…>`.
  readonly property var modeTools: ({
    normal: [],
    draw: [
      { id: "select", tip: "Selection (V)" },
      { id: "directSelect", tip: "Direct Selection (A)" },
      { id: "pen", tip: "Pen (P)" },
      { id: "pencil", tip: "Pencil (N)" },
      { id: "rectangle", tip: "Rectangle (M)" },
      { id: "ellipse", tip: "Ellipse (L)" },
      { id: "polygon", tip: "Polygon" },
      { id: "star", tip: "Star" },
      { id: "line", tip: "Line Segment (\\)" },
      { id: "text", tip: "Type (T)" },
      { id: "eyedropper", tip: "Eyedropper (I)" },
      { id: "hand", tip: "Hand (H)" },
      { id: "zoom", tip: "Zoom (Z)" }
    ],
    capture: [
      { id: "pickColor", tip: "Pick Colour: click for fill, Shift-click for stroke, right-click for a new swatch" },
      { id: "screenshot", tip: "Screenshot Region: opens it in Omastrator and traces it" },
      { id: "pasteSvg", tip: "Paste SVG as editable paths" },
      { id: "swatches", tip: "Theme Swatches: the Omarchy theme's colours as a swatch group" },
      { id: "vectorize", tip: "Vectorize with AI the screenshot just traced: click for Logo & icon, Shift-click for Sketch & line art", offerOnly: true }
    ],
    ai: [
      { id: "generate", tip: "Generate…: describe it in Omastrator, and your agent draws variations" },
      { id: "edit", tip: "Edit with Instruction…" },
      { id: "roast", tip: "Roast My Design" },
      { id: "vectorize", tip: "Vectorize with AI: click for Logo & icon, Shift-click for Sketch & line art" },
      { id: "stop", tip: "Stop waiting for the agent", waitingOnly: true }
    ],
    live: []
  })

  readonly property var tools: modeTools[mode] || []

  // ------------------------------------------------------------ activity

  property string activityText: ""
  readonly property real startedAt: Date.now()

  function flash(text, seconds) {
    if (!text) return
    activityText = text
    activityTimer.interval = Math.max(1, seconds || 3) * 1000
    activityTimer.restart()
  }

  Timer { id: activityTimer; onTriggered: root.activityText = "" }

  function plural(count, one, many) {
    return count + " " + (count === 1 ? one : many)
  }

  // Turns what changed into the one line the island shows briefly.
  function noticeChange(prev, next) {
    if (prev.mode === undefined) return
    if (next.activityId !== prev.activityId && next.activity && next.activityId > root.startedAt - 1000)
      flash(next.activity, next.activitySeconds)
    else if (next.error && next.error !== prev.error)
      flash(next.error, 6)
    else if (next.waiting && !prev.waiting)
      flash("Working with " + (next.agent || "the agent") + "…", 3)
    else if (next.variationsId && next.variationsId !== prev.variationsId && next.variations > 0)
      flash(plural(next.variations, "variation", "variations") + " ready", 4)
    else if (next.roastId && next.roastId !== prev.roastId)
      flash("Roast ready", 4)
    else if (next.proposal && next.proposal !== prev.proposal)
      flash(next.proposal + " is ready: Enter keeps it, Esc discards it", 4)
    else if (next.running && !prev.running && root.mode === "draw")
      flash("Omastrator is open", 2)
  }

  // ------------------------------------------------------------ actions

  function chooseTool(id) {
    pendingTool = id
    pendingExpiry.restart()
    status.run(["island", "tool", id])
  }

  function captureArgs(id, mouse) {
    if (id === "pickColor") {
      var target = mouse.button === Qt.RightButton ? "swatch" : (mouse.modifiers & Qt.ShiftModifier) ? "stroke" : "fill"
      return ["island", "capture", "color", target]
    }
    if (id === "screenshot") return ["island", "capture", "screenshot"]
    if (id === "pasteSvg") return ["island", "capture", "paste-svg"]
    if (id === "swatches") return ["island", "capture", "theme-swatches"]
    return []
  }

  function aiArgs(id, mouse) {
    if (id === "stop") return ["island", "ai", "cancel"]
    if (id === "vectorize") {
      var sketch = (mouse.modifiers & Qt.ShiftModifier) || mouse.button === Qt.RightButton
      return ["island", "ai", "vectorize", "--mode", sketch ? "sketch" : "logo"]
    }
    return ["island", "ai", id]
  }

  function runAction(item, mouse) {
    if (root.mode === "draw") chooseTool(item.id)
    else if (item.id === "vectorize" || root.mode === "ai") status.run(aiArgs(item.id, mouse))
    else if (root.mode === "capture") status.run(captureArgs(item.id, mouse))
  }

  // Buttons that only make sense now: Stop while an agent works, Vectorize after a traced screenshot.
  function shows(item) {
    if (item.waitingOnly) return status.value("waiting", "") !== ""
    if (item.offerOnly) return status.value("offer", "") === "vectorize"
    return true
  }

  function stepMode(direction) {
    status.run(["island", "mode", direction])
  }

  function toggleExpanded() {
    status.run(["island", root.expanded ? "rest" : "expand"])
  }

  // The first time a mode opens, its name shows beside the glyph for a while.
  readonly property bool showLabel: expanded && activityText === "" && labelsSeen.indexOf(mode) < 0
  Timer {
    running: root.showLabel
    interval: 5000
    onTriggered: status.run(["island", "seen", root.mode])
  }

  // ------------------------------------------------------------ look

  readonly property int pillHeight: Style.space(34)
  readonly property int buttonSize: Style.space(28)
  readonly property int glyphSize: Style.space(16)
  readonly property int pad: Style.space(4)
  readonly property color surface: Color.popups.background
  readonly property color ink: Color.popups.text
  readonly property color edge: Color.popups.border
  readonly property color accent: Color.accent

  // The button under the pointer; the tooltip reads its tip live.
  property Item hoveredButton: null
  readonly property string hoverTip: hoveredButton && hoveredButton.visible ? hoveredButton.tip : ""

  readonly property string focusedName: Hyprland.focusedMonitor ? String(Hyprland.focusedMonitor.name || "") : ""

  component IslandButton: Item {
    id: button
    property string glyph: ""
    property string tip: ""
    property bool selected: false
    property bool dim: false
    signal clicked(var mouse)

    width: root.buttonSize
    height: root.buttonSize

    Rectangle {
      anchors.fill: parent
      radius: height / 2
      color: button.selected ? Util.alpha(root.accent, 0.28)
           : mouse.containsMouse ? Util.alpha(root.ink, 0.1) : "transparent"
    }

    O.Glyph {
      anchors.centerIn: parent
      name: button.glyph
      size: root.glyphSize
      color: button.selected ? root.accent : root.ink
      opacity: button.dim ? 0.5 : 1
    }

    MouseArea {
      id: mouse
      anchors.fill: parent
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor
      acceptedButtons: Qt.LeftButton | Qt.RightButton
      onClicked: function (mouse) { button.clicked(mouse) }
      onContainsMouseChanged: {
        if (containsMouse) root.hoveredButton = button
        else if (root.hoveredButton === button) root.hoveredButton = null
      }
    }
  }

  Variants {
    model: Quickshell.screens

    PanelWindow {
      id: window
      required property var modelData
      screen: modelData
      // Follows the focused monitor; before Hyprland answers, the first screen.
      visible: root.focusedName === "" ? modelData === Quickshell.screens[0] : modelData.name === root.focusedName

      anchors.top: true
      // Sits under the bar's reserved space without reserving any itself.
      exclusionMode: ExclusionMode.Normal
      exclusiveZone: 0
      margins.top: Style.gapsOut
      implicitWidth: Math.max(pill.width, tipCard.width) + Style.space(24)
      implicitHeight: pill.height + Style.space(40)
      color: "transparent"
      WlrLayershell.layer: WlrLayer.Top
      WlrLayershell.namespace: "omastrator-island"
      WlrLayershell.keyboardFocus: WlrKeyboardFocus.None

      // Only the pill takes the pointer; the tooltip room lets clicks through.
      mask: Region { item: pill }

      Rectangle {
        id: pill
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        height: root.pillHeight
        width: row.implicitWidth + root.pad * 2
        radius: height / 2
        color: root.surface
        border.color: Util.alpha(root.edge, 0.6)
        border.width: 1

        Behavior on width { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

        Row {
          id: row
          anchors.centerIn: parent
          spacing: Style.space(2)

          IslandButton {
            visible: root.expanded && root.activityText === ""
            glyph: "previous"
            tip: "Previous mode"
            onClicked: root.stepMode("previous")
          }

          IslandButton {
            glyph: root.mode
            tip: root.modeTips[root.mode] || ""
            selected: root.expanded && root.mode !== "normal" && root.activityText === ""
            onClicked: root.toggleExpanded()
          }

          Text {
            visible: root.showLabel || root.activityText !== ""
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: Style.space(2)
            rightPadding: Style.space(8)
            text: root.activityText !== "" ? root.activityText : root.modeNames[root.mode]
            color: root.ink
            font.family: Style.font.family
            font.pixelSize: Style.font.body
            elide: Text.ElideRight
            width: Math.min(implicitWidth, Style.space(420))
          }

          IslandButton {
            visible: root.expanded && root.activityText === ""
            glyph: "next"
            tip: "Next mode"
            onClicked: root.stepMode("next")
          }

          Rectangle {
            visible: root.expanded && root.activityText === "" && root.tools.length > 0
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: root.buttonSize * 0.6
            color: Util.alpha(root.ink, 0.2)
          }

          Repeater {
            model: root.expanded && root.activityText === "" ? root.tools : []

            IslandButton {
              required property var modelData
              visible: root.shows(modelData)
              glyph: modelData.icon || modelData.id
              tip: modelData.tip
              selected: root.mode === "draw" && root.running && root.tool === modelData.id
              dim: modelData.enabled === false
              onClicked: function (mouse) { if (modelData.enabled !== false) root.runAction(modelData, mouse) }
            }
          }
        }
      }

      Rectangle {
        id: tipCard
        visible: root.hoverTip !== ""
        anchors.horizontalCenter: pill.horizontalCenter
        anchors.top: pill.bottom
        anchors.topMargin: Style.space(6)
        width: tipText.implicitWidth + Style.space(16)
        height: tipText.implicitHeight + Style.space(8)
        radius: Style.space(6)
        color: Color.tooltip.background
        border.color: Util.alpha(Color.tooltip.border, 0.4)
        border.width: 1

        Text {
          id: tipText
          anchors.centerIn: parent
          text: root.hoverTip
          color: Color.tooltip.text
          font.family: Style.font.family
          font.pixelSize: Style.font.caption
        }
      }
    }
  }
}
