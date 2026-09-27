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
      { id: "window", tip: "Capture to Omastrator: the focused window, to redesign" },
      { id: "pasteSvg", tip: "Paste SVG as editable paths" },
      { id: "swatches", tip: "Theme Swatches: the Omarchy theme's colours as a swatch group" },
      { id: "vectorize", tip: "Vectorize with AI the screenshot just traced: click for Logo & icon, Shift-click for Sketch & line art", offerOnly: true }
    ],
    ai: [
      { id: "generate", tip: "Generate…: describe it in Omastrator, and your agent draws variations" },
      { id: "edit", tip: "Edit with Instruction…" },
      { id: "roast", tip: "Roast My Design" },
      { id: "vectorize", tip: "Vectorize with AI: click for Logo & icon, Shift-click for Sketch & line art" },
      { id: "dictate", tip: "Dictate: hold while you speak; Omastrator shows what it heard before it acts" },
      { id: "handoff", tip: "Hand to Agent…: the document as a mockup, for an app whose source you have" },
      { id: "stop", tip: "Stop waiting for the agent", waitingOnly: true }
    ],
    live: [
      { id: "live", tip: "Open a page or project in Live", idleOnly: true },
      { id: "element", tip: "Select elements: click to select, Shift-click to add (click again to browse the page normally)", runningOnly: true },
      { id: "deploy", tip: "Deploy: writes your live edits into the code, commits, pushes, and deploys to production with your project's setup", label: "Deploy", primary: true, projectOnly: true },
      { id: "changes", icon: "review", tip: "Review changes: the diff of every write-back, with Discard", projectOnly: true },
      { id: "history", tip: "History: commits, what was deployed, and Restore", projectOnly: true },
      { id: "stop", tip: "Stop Live", runningOnly: true }
    ]
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
    else if (liveLine(prev.live || {}, next.live || {}))
      flash(liveLine(prev.live || {}, next.live || {}), liveSeconds(next.live || {}))
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

  function liveLine(prev, next) {
    var before = prev.deploy || {}, after = next.deploy || {}
    if (after.message && after.message !== before.message) return after.message
    if (next.state === prev.state) return ""
    if (next.state === "starting") return "Live: " + (next.message || "starting…")
    if (next.state === "failed") return next.message || "Live couldn't start"
    if (next.state === "running") return "Live: " + next.url + (next.mockup ? " (mock-up)" : "")
    if (next.state === "off" && prev.state === "running") return next.message || "Live stopped"
    return ""
  }

  // A deploy's steps stay up until the next one; its result stays a little longer than most lines.
  function liveSeconds(next) {
    var deploy = next.deploy || {}
    if (deploy.running) return 600
    if (deploy.failed) return 10
    return deploy.message ? 8 : 4
  }

  readonly property bool deployFailedShown: !!(root.live.deploy && root.live.deploy.failed) && activityText === root.live.deploy.message

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
    if (id === "window") return ["island", "capture", "window"]
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

  property bool selecting: true

  function liveArgs(id) {
    if (id === "stop") return ["island", "live", "stop"]
    if (id === "deploy") return ["island", "live", "deploy"]
    if (id === "changes") return ["island", "live", "changes"]
    if (id === "history") return ["island", "live", "history"]
    if (id === "element") { root.selecting = !root.selecting; return ["island", "live", "select", root.selecting ? "on" : "off"] }
    return ["island", "live", "start"]
  }

  function runAction(item, mouse) {
    if (item.id === "dictate") return
    if (root.mode === "draw") chooseTool(item.id)
    else if (root.mode === "live") status.run(liveArgs(item.id))
    else if (item.id === "vectorize" || root.mode === "ai") status.run(aiArgs(item.id, mouse))
    else if (root.mode === "capture") status.run(captureArgs(item.id, mouse))
  }

  // Buttons that only make sense now: Stop while an agent works, Vectorize after a traced screenshot.
  readonly property var live: status.value("live", {}) || {}
  readonly property string liveState: live.state || "off"
  readonly property bool deploying: !!(live.deploy && live.deploy.running)

  function shows(item) {
    if (item.runningOnly) return root.liveState === "running"
    if (item.projectOnly) return !!(root.live.project || root.live.deployProject)
    if (item.idleOnly) return root.liveState !== "running" && root.liveState !== "starting"
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

  // Leaving waits a moment, so crossing the gap between two buttons doesn't blink the tip.
  Timer {
    id: hoverLeave
    interval: 150
    property Item leaving: null
    onTriggered: if (root.hoveredButton === leaving) root.hoveredButton = null
  }

  function hoverChanged(button, inside) {
    if (inside) {
      hoverLeave.stop()
      hoveredButton = button
    } else if (hoveredButton === button) {
      hoverLeave.leaving = button
      hoverLeave.restart()
    }
  }

  readonly property string focusedName: Hyprland.focusedMonitor ? String(Hyprland.focusedMonitor.name || "") : ""

  component IslandButton: Item {
    id: button
    property string glyph: ""
    property string tip: ""
    property bool selected: false
    property bool dim: false
    // The mode's main action: its name beside the glyph, on the accent colour.
    property string label: ""
    property bool primary: false
    signal clicked(var mouse)
    // Press and release, for push-to-talk.
    signal held(bool down)

    width: label !== "" ? labelText.x + labelText.implicitWidth + Style.space(10) : root.buttonSize
    height: root.buttonSize

    Rectangle {
      anchors.fill: parent
      radius: height / 2
      color: button.primary ? Util.alpha(root.accent, mouse.containsMouse ? 0.42 : 0.28)
           : button.selected ? Util.alpha(root.accent, 0.28)
           : mouse.containsMouse ? Util.alpha(root.ink, 0.1) : "transparent"
    }

    O.Glyph {
      id: buttonGlyph
      anchors.verticalCenter: parent.verticalCenter
      x: button.label !== "" ? Style.space(8) : (parent.width - width) / 2
      name: button.glyph
      size: root.glyphSize
      color: button.selected || button.primary ? root.accent : root.ink
      opacity: button.dim ? 0.5 : 1
    }

    Text {
      id: labelText
      visible: button.label !== ""
      anchors.verticalCenter: parent.verticalCenter
      x: buttonGlyph.x + buttonGlyph.width + Style.space(5)
      text: button.label
      color: root.ink
      opacity: button.dim ? 0.5 : 1
      font.family: Style.font.family
      font.pixelSize: Style.font.body
      font.weight: Font.DemiBold
    }

    MouseArea {
      id: mouse
      anchors.fill: parent
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor
      acceptedButtons: Qt.LeftButton | Qt.RightButton
      onClicked: function (mouse) { button.clicked(mouse) }
      onPressed: button.held(true)
      onReleased: button.held(false)
      onContainsMouseChanged: root.hoverChanged(button, containsMouse)
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

      // A strip as wide as the screen and a fixed height: the surface never
      // resizes on hover. Sizing it to the tooltip moved the pill away from
      // the pointer, which hid the tooltip, which moved it back, many times a second.
      anchors.top: true
      anchors.left: true
      anchors.right: true
      // Sits under the bar's reserved space without reserving any itself.
      exclusionMode: ExclusionMode.Normal
      exclusiveZone: 0
      margins.top: Style.gapsOut
      implicitHeight: root.pillHeight + Style.space(40)
      color: "transparent"
      WlrLayershell.layer: WlrLayer.Top
      WlrLayershell.namespace: "omastrator-island"
      WlrLayershell.keyboardFocus: WlrKeyboardFocus.None

      // Only the pill takes the pointer; the rest of the strip lets clicks through.
      mask: Region { item: pill }

      Rectangle {
        id: pill
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        height: root.pillHeight
        width: row.implicitWidth + root.pad * 2
        radius: height / 2
        color: root.surface
        // Themes whose popups have no border still get an edge on a dark desktop.
        border.color: root.edge.a > 0.05 && !Qt.colorEqual(Qt.rgba(root.edge.r, root.edge.g, root.edge.b, 1), Qt.rgba(root.surface.r, root.surface.g, root.surface.b, 1))
                      ? Util.alpha(root.edge, 0.6) : Util.alpha(root.ink, 0.16)
        border.width: 1
        // The row takes its new width at once while the pill animates to it.
        clip: true

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

            // While "Heard: …" waits, a click on it cancels, as Esc does. A failed deploy's line opens its log;
            // a deploy's progress steps aside for the tools.
            MouseArea {
              anchors.fill: parent
              enabled: status.value("dictation", "idle") === "heard" || root.deployFailedShown || root.deploying
              cursorShape: Qt.PointingHandCursor
              onClicked: {
                if (root.deployFailedShown) status.run(["island", "live", "details"])
                else if (status.value("dictation", "idle") === "heard") status.run(["island", "dictate", "cancel"])
                else root.activityText = ""
              }
            }
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
              label: modelData.label || ""
              primary: !!modelData.primary
              tip: modelData.id === "deploy" && root.deploying ? "Deploying: " + (root.live.deploy.message || "")
                   : modelData.id === "deploy" && root.live.deploy && root.live.deploy.failed ? root.live.deploy.message + " Click the island's message for details."
                   : modelData.tip
              selected: (root.mode === "draw" && root.running && root.tool === modelData.id)
                        || (root.mode === "live" && modelData.id === "element" && root.selecting)
                        || (modelData.id === "dictate" && status.value("dictation", "idle") === "listening")
              onHeld: function (down) { if (modelData.id === "dictate") status.run(["island", "dictate", down ? "start" : "stop"]) }
              dim: modelData.enabled === false || (modelData.id === "deploy" && root.deploying)
              onClicked: function (mouse) { if (!dim) root.runAction(modelData, mouse) }
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
