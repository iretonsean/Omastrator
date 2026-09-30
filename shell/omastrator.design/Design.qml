import QtQuick
import Quickshell
import "../omastrator-ui" as O
import "OverlayLogic.js" as Logic

// Design mode everywhere (docs/ANYWHERE.md), as a service plugin: the status
// stream and the click-through overlay on every monitor. It replaces the
// desktop island's pill (docs/OS-SUITE.md), which is gone; "Design over…" in
// the app will take over the rest.
Item {
  id: root

  property var shell
  property var manifest
  property string omarchyPath

  O.Status {
    id: status
    onChanged: function (prev, next) { root.noticeChange(prev, next) }
  }

  Overlay { status: status }

  // What design mode says about itself (it turning on, its messages, a proposal on the overlay) reaches the
  // person as a desktop notification, since the pill that showed these lines is gone.
  function noticeChange(prev, next) {
    if (prev.mode === undefined) return
    var line = designLine(prev.design || {}, next.design || {})
    if (line) notify(line, 4)
  }

  function designLine(prev, next) {
    if (next.on && !prev.on) return Logic.designOnLine(next)
    if (next.message && next.message !== prev.message) return next.message
    if (next.proposal && !prev.proposal) return (next.proposal.title || "A proposal") + " is on the overlay: Keep or Discard it in the bar"
    return ""
  }

  // The first line is the summary and the rest the body, as `omastrator island activity` sends them.
  function notify(text, seconds) {
    var lines = String(text).split("\n")
    var command = ["notify-send", "-a", "Omastrator", "-t", String(seconds * 1000),
                   "-h", "string:x-canonical-private-synchronous:omastrator", lines[0]]
    var body = lines.slice(1).join("\n").trim()
    if (body) command.push(body)
    Quickshell.execDetached(command)
  }
}
