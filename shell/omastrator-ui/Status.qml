import QtQuick
import Quickshell
import Quickshell.Io

// `omastrator status --follow`, one JSON object per line (docs/OS-SUITE.md).
// The binary is `omastrator` on PATH unless `omastrator setup` wrote another
// in ~/.config/omastrator/shell.json.
Item {
  id: root

  property var status: ({})
  property bool connected: false
  property string binary: "omastrator"
  property bool binaryKnown: false

  readonly property string configHome: Quickshell.env("XDG_CONFIG_HOME") || (Quickshell.env("HOME") + "/.config")

  // A binary named in shell.json after a failed start is tried at once.
  onBinaryChanged: if (root.binaryKnown && !stream.running) { restart.stop(); restart.interval = 3000; stream.running = true }

  // prev is {} for the first line.
  signal changed(var prev, var next)

  function value(key, fallback) {
    var v = status[key]
    return v === undefined ? fallback : v
  }

  function run(args) {
    Quickshell.execDetached([root.binary].concat(args))
  }

  function apply(line) {
    var next
    try { next = JSON.parse(line) } catch (e) { return }
    var prev = root.status
    root.status = next
    root.connected = true
    restart.interval = 3000
    root.changed(prev, next)
  }

  FileView {
    path: root.configHome + "/omastrator/shell.json"
    printErrors: false
    watchChanges: true
    onFileChanged: reload()
    onLoaded: {
      try {
        var config = JSON.parse(text())
        if (config.binary) root.binary = String(config.binary)
      } catch (e) {}
      root.binaryKnown = true
    }
    onLoadFailed: root.binaryKnown = true
  }

  Process {
    id: stream
    command: [root.binary, "status", "--follow"]
    running: root.binaryKnown
    stdout: SplitParser { onRead: function (data) { root.apply(data) } }
    // A binary that can't start never sends exited, so this watches running instead.
    onRunningChanged: {
      if (running) return
      root.connected = false
      restart.start()
      // Backs off to a minute, so a missing binary doesn't fill the shell's log.
      restart.interval = Math.min(restart.interval * 2, 60000)
    }
  }

  // The stream only ends if the binary went away or was replaced; it retries until it's back.
  Timer {
    id: restart
    interval: 3000
    onTriggered: stream.running = true
  }
}
