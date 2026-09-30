.pragma library

// The page dots' decisions (docs/WORKSPACES.md, "In the bar"), kept out of the QML so ShellPluginTests can run them.

// The status stream's pageWorkspaces, in page order, and only well-formed entries.
function pages(status) {
  var list = status && Array.isArray(status.pageWorkspaces) ? status.pageWorkspaces : []
  var result = []
  for (var i = 0; i < list.length; i++) {
    var entry = list[i]
    if (entry && typeof entry.name === "string" && entry.name !== "")
      result.push({ name: entry.name, page: String(entry.page || ""), document: String(entry.document || "") })
  }
  return result
}

function documentCount(list) {
  var seen = {}
  var count = 0
  for (var i = 0; i < list.length; i++) {
    if (!seen[list[i].document]) { seen[list[i].document] = true; count++ }
  }
  return count
}

// What each button shows. `focusedName` is Hyprland's focused workspace name; the glyph is the one Omarchy's numbers use.
function buttons(list, focusedName) {
  var several = documentCount(list) > 1
  return list.map(function (entry) {
    var focused = entry.name === focusedName
    var label = entry.page !== "" ? entry.page : entry.name
    return {
      name: entry.name,
      focused: focused,
      text: focused ? "󱓻" : "•",
      opacity: focused ? 1 : 0.5,
      tooltip: several && entry.document !== "" ? label + " (" + entry.document + ")" : label
    }
  })
}

function luaEscape(text) {
  return String(text).replace(/\\/g, "\\\\").replace(/"/g, "\\\"").replace(/\n/g, "\\n").replace(/\r/g, "\\r")
}

// The Lua Omarchy's own widget sends, with the workspace named instead of numbered.
function focusLua(name) {
  return "hl.dsp.focus({ workspace = \"name:" + luaEscape(name) + "\" })"
}
