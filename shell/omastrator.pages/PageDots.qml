import QtQuick
import Quickshell
import Quickshell.Hyprland
import qs.Commons
import qs.Ui
import "../omastrator-ui" as O
import "PageDotsLogic.js" as Logic

// Pages as Workspaces in the bar (docs/WORKSPACES.md): a button for each page that holds a workspace, drawn like
// Omarchy's own workspace numbers, right after them. Named workspaces have negative ids, so Omarchy's widget never lists them.
BarWidget {
  id: root
  moduleName: "omastrator.pages"

  O.Status { id: status }

  readonly property var pageList: Logic.pages(status.status)
  readonly property string focusedName: Hyprland.focusedWorkspace ? Hyprland.focusedWorkspace.name : ""
  readonly property var pageButtons: Logic.buttons(pageList, focusedName)

  // Wider than the numbers' own spacing, so the pages read as a group.
  readonly property real leadingGap: pageList.length === 0 || root.vertical ? 0 : Style.spaceReal(3)
  readonly property real trailingGap: pageList.length === 0 || root.vertical ? 0 : Style.spaceReal(1.5)

  visible: pageList.length > 0
  implicitWidth: pageList.length === 0 ? 0 : grid.implicitWidth + leadingGap + trailingGap
  implicitHeight: pageList.length === 0 ? 0 : grid.implicitHeight

  // Through the app when it runs, so there is no stand-in phase; Hyprland's focus when it doesn't.
  function focusPage(name) {
    Quickshell.execDetached(Logic.pageCommand(status.binary, name))
  }

  // A positioner, not Omarchy's GridLayout: the pages arrive after the widget loads, and a GridLayout kept its first (empty) size.
  Grid {
    id: grid
    anchors.fill: parent
    anchors.leftMargin: root.leadingGap
    anchors.rightMargin: root.trailingGap
    columns: root.vertical ? 1 : Math.max(1, root.pageButtons.length)
    columnSpacing: root.vertical ? 0 : Style.space(1)
    rowSpacing: root.vertical ? Style.space(2) : 0

    Repeater {
      model: root.pageButtons

      WidgetButton {
        required property var modelData

        bar: root.bar
        text: modelData.text
        tooltipText: modelData.tooltip
        opacity: modelData.opacity
        horizontalMargin: 6
        verticalPadding: 6
        fixedWidth: root.vertical ? root.barSize : Style.space(20)
        fixedHeight: root.barSize
        onPressed: function() { root.focusPage(modelData.name) }
      }
    }
  }
}
