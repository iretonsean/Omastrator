import QtQuick
import QtQuick.Shapes
import Quickshell
import Quickshell.Wayland
import qs.Commons
import "OverlayLogic.js" as Logic

// Design mode everywhere (docs/ANYWHERE.md): a transparent layer over every
// monitor. It shows the art drawn on each surface, and on the monitor in
// design mode the hover outline, Alt distances, the floating bar, the Inspect
// card and onboarding. Its input mask is empty unless a bar, a card or a
// drawing tool needs the pointer, so the apps underneath keep every click.
Item {
  id: root

  required property var status
  // The island's pill, from Island.qml: the overlay leaves it out of its input.
  property int islandWidth: 0
  property int islandHeight: 0

  readonly property var design: status.value("design", {}) || {}
  readonly property bool on: !!design.on
  readonly property var overlays: design.overlays || []
  readonly property var monitorList: design.monitors || []
  readonly property var bar: design.bar || null
  readonly property var hover: design.hover || null
  readonly property var anchorThing: design.anchor || null
  readonly property var lines: design.distances || []
  readonly property var onboarding: design.onboarding || ({})
  readonly property var detail: design.detail || null
  readonly property var proposal: design.proposal || null
  // Where a proposal waits when the floating bar isn't up: the design monitor, else the first.
  readonly property string proposalScreen: design.monitor || (Quickshell.screens.length ? Quickshell.screens[0].name : "")
  readonly property string tool: design.tool || "inspect"
  readonly property var lifting: design.lift || null
  readonly property var look: design.look || null

  readonly property color ink: Color.popups.text
  readonly property color paper: Color.popups.background
  readonly property color edge: Util.alpha(Color.popups.text, 0.14)
  readonly property color accent: Color.accent
  readonly property color measureColor: Color.urgent
  readonly property int pad: Style.space(6)

  function run(args) {
    status.run(args)
  }

  // Where a screen sits in Hyprland's layout; the app's monitor list first, Qt's screen otherwise.
  function placeOf(screen) {
    for (var i = 0; i < monitorList.length; ++i) {
      if (monitorList[i].name === screen.name)
        return monitorList[i]
    }
    return { name: screen.name, x: screen.x, y: screen.y, width: screen.width, height: screen.height }
  }

  // The bar holds still while the pointer is on it, and settles a moment before following a new target,
  // so crossing other things on the way to it doesn't move it.
  // The floating bar's own state, kept across shell reloads: folded down, and pinned where it was dragged.
  PersistentProperties {
    id: remembered
    reloadableId: "omastratorFloatingBar"
    property bool collapsed: false
    property bool pinned: false
    property real pinX: 0
    property real pinY: 0
  }

  property var shownBar: null
  property bool barHeld: false
  onBarChanged: if (!barHeld) settle.restart()
  Timer {
    id: settle
    interval: 220
    onTriggered: root.shownBar = root.bar
  }

  component Chip: Rectangle {
    id: chip
    property string label: ""
    property string tip: ""
    property bool primary: false
    property bool chosen: false
    property bool dim: false
    signal clicked

    implicitWidth: chipText.implicitWidth + Style.space(16)
    implicitHeight: Style.space(24)
    radius: height / 2
    color: chip.primary ? Util.alpha(root.accent, chipMouse.containsMouse ? 0.5 : 0.35)
         : chip.chosen ? Util.alpha(root.accent, 0.22)
         : chipMouse.containsMouse ? Util.alpha(root.ink, 0.1) : Util.alpha(root.ink, 0.04)
    border.color: chip.chosen ? root.accent : "transparent"
    border.width: 1
    opacity: chip.dim ? 0.45 : 1

    Text {
      id: chipText
      anchors.centerIn: parent
      text: chip.label
      color: root.ink
      font.family: Style.font.family
      font.pixelSize: Style.font.caption
    }

    MouseArea {
      id: chipMouse
      anchors.fill: parent
      hoverEnabled: true
      cursorShape: chip.dim ? Qt.ArrowCursor : Qt.PointingHandCursor
      onClicked: if (!chip.dim) chip.clicked()
    }
  }

  component Label: Text {
    color: root.ink
    font.family: Style.font.family
    font.pixelSize: Style.font.caption
  }

  Variants {
    model: Quickshell.screens

    PanelWindow {
      id: window

      required property var modelData
      readonly property var place: root.placeOf(modelData)
      readonly property bool mine: root.on && root.design.monitor === modelData.name
      readonly property bool drawing: mine && root.tool !== "inspect"
      readonly property bool panelsShown: barCard.visible || onboardingCard.visible || detailCard.visible || typing.visible || gapGrip.visible
      readonly property string maskMode: Logic.maskMode(root.design, place, panelsShown)
      readonly property var myArt: root.overlays.filter(function (each) { return each.monitor === window.modelData.name })
      // Below the bar's reserved space and the island: the floating bar never covers them.
      readonly property int topClear: (place.reservedTop || 0) + Style.gapsOut + root.islandHeight + Style.space(12)

      screen: modelData
      visible: mine || myArt.length > 0 || (root.proposal !== null && root.proposalScreen === modelData.name)
      anchors.top: true
      anchors.left: true
      anchors.right: true
      anchors.bottom: true
      exclusionMode: ExclusionMode.Ignore
      color: "transparent"
      WlrLayershell.layer: WlrLayer.Overlay
      WlrLayershell.namespace: "omastrator-overlay"
      WlrLayershell.keyboardFocus: Logic.wantsKeyboard(root.design, place, askField.activeFocus || onboardingCard.visible || typing.visible)
                                   ? WlrKeyboardFocus.OnDemand : WlrKeyboardFocus.None

      // Click-through by default: nothing in the mask. The bar and cards join it while shown; a drawing tool takes it all.
      mask: Region {
        item: window.maskMode === "full" ? everything : null

        Region { item: window.maskMode === "panels" && barCard.visible ? barCard : null }
        Region { item: window.maskMode === "panels" && detailCard.visible ? detailCard : null }
        Region { item: window.maskMode === "panels" && onboardingCard.visible ? onboardingCard : null }
        Region { item: window.maskMode === "panels" && typing.visible ? typing : null }
        Region { item: window.maskMode === "panels" && gapGrip.visible ? gapGrip : null }
        // A proposal left waiting keeps its own Keep and Discard, with or without design mode.
        Region { item: proposalCard.visible ? proposalCard : null }
        // The island stays reachable over everything else: its buttons are how to change tool or leave.
        Region { item: islandHole; intersection: Intersection.Subtract }
      }

      Item {
        id: everything
        anchors.fill: parent
      }

      // Esc leaves design mode from the overlay itself, so it works even where Hyprland has no design keys.
      Item {
        id: escapeKeys
        focus: window.drawing && !typing.visible
        Keys.onEscapePressed: root.run(["design", "off"])
      }

      Item {
        id: islandHole
        readonly property var box: Logic.islandHole(window.place, root.islandWidth, root.islandHeight, Style.gapsOut)
        x: box.x
        y: box.y
        width: box.width
        height: box.height
      }

      // ------------------------------------------------------------ art

      Repeater {
        model: window.myArt

        Image {
          required property var modelData
          x: modelData.rect[0] - window.place.x
          y: modelData.rect[1] - window.place.y
          width: modelData.rect[2]
          height: modelData.rect[3]
          source: "file://" + modelData.png
          cache: false
          smooth: true

          Rectangle {
            visible: root.on && parent.modelData.selected
            anchors.fill: parent
            anchors.margins: -2
            color: "transparent"
            border.color: root.accent
            border.width: 1
            radius: 3
          }
        }
      }

      // ------------------------------------------------------------ inspect and measure

      Rectangle {
        id: hoverBox
        readonly property var box: root.hover ? Logic.local(root.hover.bounds, window.place) : null
        visible: window.mine && !window.drawing && box !== null
        x: box ? box.x : 0
        y: box ? box.y : 0
        width: box ? box.width : 0
        height: box ? box.height : 0
        color: Util.alpha(root.accent, 0.06)
        border.color: root.accent
        border.width: 1.5
      }

      Rectangle {
        visible: hoverBox.visible && sizeText.text !== ""
        x: Math.max(4, Math.min(hoverBox.x, window.width - width - 4))
        y: hoverBox.y + hoverBox.height + 4 + height > window.height ? hoverBox.y - height - 4 : hoverBox.y + hoverBox.height + 4
        width: sizeText.implicitWidth + Style.space(10)
        height: sizeText.implicitHeight + Style.space(4)
        radius: 3
        color: root.accent

        Text {
          id: sizeText
          anchors.centerIn: parent
          text: Logic.sizeLabel(root.hover)
          color: Color.popups.background
          font.family: Style.font.family
          font.pixelSize: Style.font.caption
          font.weight: Font.DemiBold
        }
      }

      Rectangle {
        readonly property var box: root.anchorThing ? Logic.local(root.anchorThing.bounds, window.place) : null
        visible: window.mine && box !== null && root.design.measuring
        x: box ? box.x : 0
        y: box ? box.y : 0
        width: box ? box.width : 0
        height: box ? box.height : 0
        color: "transparent"
        border.color: root.measureColor
        border.width: 1.5
      }

      Repeater {
        model: window.mine ? root.lines : []

        Item {
          required property var modelData
          readonly property bool across: modelData.y1 === modelData.y2
          x: Math.min(modelData.x1, modelData.x2) - window.place.x
          y: Math.min(modelData.y1, modelData.y2) - window.place.y
          width: Math.max(1, Math.abs(modelData.x2 - modelData.x1))
          height: Math.max(1, Math.abs(modelData.y2 - modelData.y1))

          Rectangle {
            anchors.fill: parent
            color: root.measureColor
          }

          Rectangle {
            x: parent.width / 2 - width / 2 + (parent.across ? 0 : width / 2 + 4)
            y: parent.height / 2 - height / 2 + (parent.across ? height / 2 + 4 : 0)
            width: valueText.implicitWidth + Style.space(8)
            height: valueText.implicitHeight + Style.space(2)
            radius: 3
            color: root.measureColor

            Text {
              id: valueText
              anchors.centerIn: parent
              text: parent.parent.modelData.value
              color: "white"
              font.family: Style.font.family
              font.pixelSize: Style.font.caption
              font.weight: Font.DemiBold
            }
          }
        }
      }

      // ------------------------------------------------------------ drawing

      MouseArea {
        id: canvas
        property var points: []
        anchors.fill: parent
        enabled: window.drawing && !typing.visible
        cursorShape: Qt.CrossCursor
        onPressed: function (mouse) {
          canvas.points = [{ x: mouse.x + window.place.x, y: mouse.y + window.place.y }]
        }
        onPositionChanged: function (mouse) {
          canvas.points = Logic.addPoint(canvas.points, mouse.x + window.place.x, mouse.y + window.place.y, 3)
        }
        onReleased: function (mouse) {
          var last = { x: mouse.x + window.place.x, y: mouse.y + window.place.y }
          var stroke = root.tool === "pen" ? Logic.addPoint(canvas.points, last.x, last.y, 1) : [canvas.points[0], last]
          canvas.points = []
          if (root.tool === "text" || root.tool === "note") {
            typing.points = stroke
            typing.visible = true
            typingField.text = ""
            typingField.forceActiveFocus()
            return
          }
          root.run(Logic.drawArgs(root.tool, stroke, ""))
        }
      }

      Shape {
        visible: canvas.points.length > 0
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
          strokeColor: root.accent
          strokeWidth: 3
          fillColor: "transparent"
          capStyle: ShapePath.RoundCap
          joinStyle: ShapePath.RoundJoin
          PathPolyline {
            path: {
              var p = canvas.points
              if (p.length === 0) return []
              var first = Qt.point(p[0].x - window.place.x, p[0].y - window.place.y)
              var last = Qt.point(p[p.length - 1].x - window.place.x, p[p.length - 1].y - window.place.y)
              if (root.tool === "pen") return p.map(function (each) { return Qt.point(each.x - window.place.x, each.y - window.place.y) })
              if (root.tool === "rectangle" || root.tool === "note")
                return [first, Qt.point(last.x, first.y), last, Qt.point(first.x, last.y), first]
              if (root.tool === "ellipse") {
                var cx = (first.x + last.x) / 2, cy = (first.y + last.y) / 2
                var rx = Math.abs(last.x - first.x) / 2, ry = Math.abs(last.y - first.y) / 2
                var ring = []
                for (var i = 0; i <= 48; ++i)
                  ring.push(Qt.point(cx + rx * Math.cos(i * Math.PI / 24), cy + ry * Math.sin(i * Math.PI / 24)))
                return ring
              }
              return [first, last]
            }
          }
        }
      }

      // Text and notes: type where it was clicked, Enter draws it, Esc drops it.
      Rectangle {
        id: typing
        property var points: []
        visible: false
        // Esc goes to Hyprland's design keys first, so the field's own Esc may never run: close with the mode.
        Connections {
          target: window
          function onMineChanged() { if (!window.mine) typing.visible = false }
        }
        Connections {
          target: root
          function onToolChanged() { typing.visible = false }
        }
        x: points.length > 0 ? points[0].x - window.place.x : 0
        y: points.length > 0 ? points[0].y - window.place.y : 0
        width: Math.max(Style.space(180), typingField.implicitWidth + Style.space(16))
        height: typingField.implicitHeight + Style.space(10)
        radius: 4
        color: root.tool === "note" ? "#ffe88c" : root.paper
        border.color: root.accent
        border.width: 1

        TextInput {
          id: typingField
          anchors.fill: parent
          anchors.margins: Style.space(5)
          color: root.tool === "note" ? "#332b14" : root.ink
          font.family: Style.font.family
          font.pixelSize: Style.font.body
          Keys.onReturnPressed: {
            if (text.trim() !== "")
              root.run(Logic.drawArgs(root.tool, typing.points, text.trim()))
            typing.visible = false
          }
          Keys.onEscapePressed: typing.visible = false
        }
      }

      // ------------------------------------------------------------ Desktop Look's gap handle

      GapHandle {
        id: gapGrip
        handleData: window.mine && !window.drawing && root.shownBar && root.shownBar.kind === "window"
                    ? Logic.gapHandle(root.look, root.shownBar.bounds, window.place) : null
        accent: root.accent
        onDropped: function (args) { root.run(args) }
      }

      // ------------------------------------------------------------ the floating bar

      Rectangle {
        id: barCard
        readonly property var target: root.shownBar
        readonly property var spot: remembered.pinned
                                    ? Logic.clampBar(remembered.pinX, remembered.pinY, width, height, window.place, window.topClear)
                                    : target ? Logic.barPosition(target.bounds, width, height, window.place, Style.space(10), window.topClear) : ({ x: 0, y: 0 })
        visible: window.mine && !window.drawing && target !== null && !root.onboarding.open
        x: spot.x
        y: spot.y
        width: barColumn.implicitWidth + root.pad * 2
        height: barColumn.implicitHeight + root.pad * 2
        radius: Style.space(10)
        color: root.paper
        border.color: root.edge
        border.width: 1

        HoverHandler {
          onHoveredChanged: {
            root.barHeld = hovered
            if (!hovered) settle.restart()
          }
        }

        Column {
          id: barColumn
          x: root.pad
          y: root.pad
          spacing: Style.space(5)

          Row {
            spacing: Style.space(6)

            // The grip: drag the bar anywhere; once dragged it stays there until Unpin.
            Text {
              id: grip
              anchors.verticalCenter: parent.verticalCenter
              text: "⠿"
              color: root.ink
              opacity: gripMouse.containsMouse || gripMouse.pressed ? 0.9 : 0.45
              font.pixelSize: Style.font.body

              MouseArea {
                id: gripMouse
                anchors.fill: parent
                anchors.margins: -Style.space(6)
                hoverEnabled: true
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                property point grab
                onPressed: function (mouse) { grab = Qt.point(mouse.x, mouse.y) }
                onPositionChanged: function (mouse) {
                  if (!pressed) return
                  var at = mapToItem(barCard.parent, mouse.x - grab.x, mouse.y - grab.y)
                  var spot = Logic.clampBar(at.x - grip.x - barColumn.x, at.y - grip.y - barColumn.y, barCard.width, barCard.height, window.place, window.topClear)
                  remembered.pinned = true
                  remembered.pinX = spot.x
                  remembered.pinY = spot.y
                }
              }
            }

            Label {
              anchors.verticalCenter: parent.verticalCenter
              text: barCard.target ? barCard.target.label : ""
              opacity: 0.7
              elide: Text.ElideRight
              width: Math.min(implicitWidth, Style.space(260))
            }

            Chip {
              label: "Deselect"
              visible: !!barCard.target && (barCard.target.kind.indexOf("art:") === 0 || !!root.design.selected)
              onClicked: root.run(["design", "deselect"])
            }

            Chip {
              visible: remembered.pinned
              label: "Unpin"
              tip: "Follow what you point at again"
              onClicked: remembered.pinned = false
            }

            Chip {
              label: remembered.collapsed ? "Expand" : "Collapse"
              tip: remembered.collapsed ? "Show the actions, suggestions and Ask" : "Fold the bar down to this line"
              onClicked: remembered.collapsed = !remembered.collapsed
            }
          }

          // Always shown, even folded down: a waiting proposal and a running lift keep their answers.
          Row {
            spacing: Style.space(4)
            visible: root.proposal !== null

            Label {
              anchors.verticalCenter: parent.verticalCenter
              text: root.proposal ? root.proposal.title + (root.proposal.summary ? ": " + root.proposal.summary : "") : ""
              width: Math.min(implicitWidth, Style.space(240))
              elide: Text.ElideRight
            }

            Chip {
              label: "Keep"
              primary: true
              onClicked: root.run(["design", "keep"])
            }

            Chip {
              label: "Discard"
              onClicked: root.run(["design", "discard"])
            }
          }

          Row {
            spacing: Style.space(4)
            visible: root.lifting !== null

            Label {
              anchors.verticalCenter: parent.verticalCenter
              text: Logic.liftText(root.lifting)
              width: Math.min(implicitWidth, Style.space(280))
              elide: Text.ElideRight
            }

            Chip {
              label: "Cancel"
              onClicked: root.run(["design", "lift", "cancel"])
            }
          }

          Column {
            spacing: Style.space(5)
            visible: !remembered.collapsed

          Row {
            spacing: Style.space(4)

            Repeater {
              model: barCard.target ? barCard.target.actions : []

              Chip {
                required property var modelData
                label: modelData.label
                tip: modelData.tip
                dim: modelData.enabled === false
                onClicked: root.run(Logic.actionArgs(barCard.target, modelData))
              }
            }
          }

          Row {
            spacing: Style.space(4)
            visible: !!barCard.target && barCard.target.suggestions.length > 0

            Repeater {
              model: barCard.target ? barCard.target.suggestions : []

              Chip {
                required property var modelData
                label: modelData.label
                chosen: true
                onClicked: root.run(Logic.chipArgs(barCard.target, modelData))
              }
            }
          }

          Rectangle {
            width: Math.max(Style.space(280), parent.width)
            height: Style.space(28)
            radius: height / 2
            color: Util.alpha(root.ink, 0.06)
            border.color: askField.activeFocus ? root.accent : "transparent"
            border.width: 1

            TextInput {
              id: askField
              anchors.fill: parent
              anchors.leftMargin: Style.space(12)
              anchors.rightMargin: Style.space(12)
              verticalAlignment: TextInput.AlignVCenter
              color: root.ink
              font.family: Style.font.family
              font.pixelSize: Style.font.caption
              clip: true
              Keys.onReturnPressed: {
                if (text.trim() === "") return
                root.run(Logic.askArgs(barCard.target, text.trim()))
                text = ""
                focus = false
              }
              Keys.onEscapePressed: focus = false
            }

            Label {
              anchors.verticalCenter: parent.verticalCenter
              x: Style.space(12)
              visible: askField.text === "" && !askField.activeFocus
              text: barCard.target ? barCard.target.placeholder : "Ask AI…"
              opacity: 0.5
            }
          }

          Row {
            spacing: Style.space(4)
            visible: !!barCard.target && (barCard.target.kind.indexOf("art:") === 0 || barCard.target.art > 0)

            Label {
              anchors.verticalCenter: parent.verticalCenter
              text: "Send to"
              opacity: 0.7
            }

            Repeater {
              model: barCard.target ? barCard.target.destinations : []

              Chip {
                required property var modelData
                label: modelData.label
                chosen: !!barCard.target && barCard.target.destination === modelData.id
                dim: modelData.enabled === false
                onClicked: {
                  if (modelData.id === "agent" && askField.text.trim() === "") {
                    askField.forceActiveFocus()
                    return
                  }
                  root.run(Logic.sendArgs(barCard.target, modelData.id, askField.text.trim()))
                  askField.text = ""
                }
              }
            }
          }



          Label {
            visible: text !== ""
            text: root.design.waiting || root.design.message || ""
            opacity: 0.75
            width: Math.min(implicitWidth, Style.space(360))
            wrapMode: Text.Wrap
          }
          }
        }
      }

      // ------------------------------------------------------------ a proposal left waiting

      // The floating bar carries Keep and Discard while design mode is on. Without it (design mode ended, or
      // the bar is elsewhere) the proposal would sit on the screen with no way to answer it.
      Rectangle {
        id: proposalCard
        visible: root.proposal !== null && !barCard.visible && root.proposalScreen === window.modelData.name
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Style.space(28)
        width: proposalRow.implicitWidth + root.pad * 2
        height: proposalRow.implicitHeight + root.pad * 2
        radius: Style.space(10)
        color: root.paper
        border.color: root.edge
        border.width: 1

        Row {
          id: proposalRow
          x: root.pad
          y: root.pad
          spacing: Style.space(6)

          Label {
            anchors.verticalCenter: parent.verticalCenter
            text: root.proposal ? root.proposal.title : ""
            width: Math.min(implicitWidth, Style.space(320))
            elide: Text.ElideRight
          }

          Chip {
            label: "Keep"
            primary: true
            onClicked: root.run(["design", "keep"])
          }

          Chip {
            label: "Discard"
            onClicked: root.run(["design", "discard"])
          }
        }
      }

      // ------------------------------------------------------------ Inspect's card

      Rectangle {
        id: detailCard
        visible: window.mine && root.detail !== null && !root.onboarding.open
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: Style.space(16)
        anchors.topMargin: Style.space(72)
        width: Style.space(260)
        height: detailColumn.implicitHeight + root.pad * 2
        radius: Style.space(10)
        color: root.paper
        border.color: root.edge
        border.width: 1

        Column {
          id: detailColumn
          x: root.pad
          y: root.pad
          width: parent.width - root.pad * 2
          spacing: Style.space(3)

          Label {
            text: root.detail ? root.detail.name || root.detail.role : ""
            font.weight: Font.DemiBold
            font.pixelSize: Style.font.body
            elide: Text.ElideRight
            width: parent.width
          }

          Repeater {
            model: {
              var d = root.detail
              if (!d) return []
              var rows = [["Size", d.bounds[2] + " × " + d.bounds[3]], ["Position", d.bounds[0] + ", " + d.bounds[1]]]
              if (d.color) rows.push(["Colour", d.color])
              if (d.background) rows.push(["Background", d.background])
              if (d.pixel) rows.push(["Under the pointer", d.pixel])
              if (d.fontFamily) rows.push(["Font", d.fontFamily + (d.fontWeight ? " " + d.fontWeight : "")])
              if (d.fontSize) rows.push(["Font size", d.fontSize + " px"])
              var styles = d.styles || {}
              for (var key in styles)
                if (styles[key] && styles[key] !== "normal" && styles[key] !== "0px" && styles[key] !== "none") rows.push([key, styles[key]])
              return rows
            }

            Row {
              required property var modelData
              spacing: Style.space(6)

              Rectangle {
                visible: String(parent.modelData[1]).charAt(0) === "#"
                width: Style.space(12)
                height: Style.space(12)
                radius: 2
                anchors.verticalCenter: parent.verticalCenter
                color: visible ? parent.modelData[1] : "transparent"
                border.color: root.edge
              }

              Label {
                text: parent.modelData[0]
                opacity: 0.6
                width: Style.space(96)
              }

              Label {
                text: parent.modelData[1]
                elide: Text.ElideRight
                width: Style.space(130)
              }
            }
          }

          Row {
            spacing: Style.space(4)

            Chip {
              label: "Copy CSS"
              visible: !!root.detail && root.detail.source === "dom"
              onClicked: root.run(["design", "action", "copyCss"])
            }

            Chip {
              label: "Close"
              onClicked: root.run(["design", "action", "closeDetail"])
            }
          }
        }
      }

      // ------------------------------------------------------------ onboarding

      Rectangle {
        id: onboardingCard
        readonly property var answers: root.onboarding.answers || ({})
        visible: window.mine && !!root.onboarding.open
        anchors.centerIn: parent
        width: Style.space(440)
        height: onboardingColumn.implicitHeight + Style.space(32)
        radius: Style.space(12)
        color: root.paper
        border.color: root.edge
        border.width: 1

        function chosen(question, option) {
          var value = answers[question.id]
          return question.multiple ? (value || []).indexOf(option.id) >= 0 : value === option.id
        }

        function choose(question, option) {
          var values = []
          if (question.multiple) {
            values = (answers[question.id] || []).slice()
            var at = values.indexOf(option.id)
            if (at >= 0) values.splice(at, 1)
            else values.push(option.id)
          } else {
            values = [option.id]
          }
          root.run(["design", "onboarding", question.id].concat(values))
        }

        Column {
          id: onboardingColumn
          x: Style.space(16)
          y: Style.space(16)
          width: parent.width - Style.space(32)
          spacing: Style.space(10)

          Text {
            text: "Design mode"
            color: root.ink
            font.family: Style.font.family
            font.pixelSize: Style.font.body * 1.2
            font.weight: Font.DemiBold
          }

          Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: (root.onboarding.note || "") + " A few questions tune the suggestions; change them any time from the island."
            opacity: 0.8
          }

          Repeater {
            model: root.onboarding.questions || []

            Column {
              id: questionColumn
              required property var modelData
              width: onboardingColumn.width
              spacing: Style.space(5)

              Label {
                text: questionColumn.modelData.text
                font.pixelSize: Style.font.body
              }

              Flow {
                width: parent.width
                spacing: Style.space(4)

                Repeater {
                  model: questionColumn.modelData.options

                  Chip {
                    required property var modelData
                    label: modelData.label
                    chosen: onboardingCard.chosen(questionColumn.modelData, modelData)
                    onClicked: onboardingCard.choose(questionColumn.modelData, modelData)
                  }
                }
              }
            }
          }

          Row {
            spacing: Style.space(6)

            Chip {
              label: "Done"
              primary: true
              onClicked: root.run(["design", "onboarding", "done"])
            }

            Chip {
              label: "Skip"
              onClicked: root.run(["design", "onboarding", "skip"])
            }
          }
        }
      }
    }
  }
}
