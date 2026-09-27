.pragma library

// The overlay's decisions, kept apart from its QML so they can be tested
// without a compositor (tests/Agent/ShellPluginTests.cpp runs them).
// `design` is the status stream's "design" object; rectangles are [x, y, w, h]
// in Hyprland's layout coordinates; `screen` is {name, x, y, width, height}.

// What the overlay on `screen` takes from the pointer:
//   "none"   nothing: every click reaches the apps underneath (the default)
//   "panels" only the floating bar and the cards on show
//   "full"   the whole monitor, while a drawing tool is chosen
function maskMode(design, screen, panelsShown) {
  if (!design || !design.on || design.monitor !== screen.name)
    return "none"
  if (design.tool && design.tool !== "inspect")
    return "full"
  return panelsShown ? "panels" : "none"
}

// Typing needs the keyboard: the Ask field, onboarding and the text tools. Otherwise the apps keep it.
function wantsKeyboard(design, screen, typing) {
  if (!design || !design.on || design.monitor !== screen.name)
    return false
  return !!typing || design.tool === "text" || design.tool === "note"
}

// A layout rectangle in the overlay's own coordinates on `screen`.
function local(rect, screen) {
  return { x: rect[0] - screen.x, y: rect[1] - screen.y, width: rect[2], height: rect[3] }
}

function onScreen(rect, screen) {
  return rect[0] < screen.x + screen.width && rect[0] + rect[2] > screen.x
      && rect[1] < screen.y + screen.height && rect[1] + rect[3] > screen.y
}

// The floating bar next to `bounds`: centred under it, above when there's no room, inside the screen.
function barPosition(bounds, barWidth, barHeight, screen, gap) {
  var box = local(bounds, screen)
  var x = box.x + box.width / 2 - barWidth / 2
  var y = box.y + box.height + gap
  if (y + barHeight > screen.height - gap)
    y = box.y - gap - barHeight
  if (y < gap)
    y = Math.min(screen.height - barHeight - gap, Math.max(gap, box.y + gap))
  x = Math.max(gap, Math.min(x, screen.width - barWidth - gap))
  return { x: Math.round(x), y: Math.round(y) }
}

// The label under a hovered box: "button.primary  120 × 40".
function sizeLabel(hover) {
  if (!hover) return ""
  var size = hover.bounds[2] + " × " + hover.bounds[3]
  var name = hover.source === "window" ? (hover.surface && hover.surface.app) : hover.name
  return name ? name + "  " + size : size
}

// The command a finished drawing runs: points on screen, as `omastrator design draw` takes them.
function drawArgs(tool, points, text) {
  var args = ["design", "draw", tool]
  for (var i = 0; i < points.length; ++i)
    args.push(Math.round(points[i].x) + "," + Math.round(points[i].y))
  if (text) {
    args.push("--text")
    args.push(text)
  }
  return args
}

// Freehand points are thinned as they come, so a stroke is a few dozen points, not thousands.
function addPoint(points, x, y, spacing) {
  if (points.length > 0) {
    var last = points[points.length - 1]
    if (Math.abs(last.x - x) + Math.abs(last.y - y) < spacing)
      return points
  }
  return points.concat([{ x: x, y: y }])
}

// The bar's command for an action, a suggestion chip or a destination.
function actionArgs(bar, action) {
  var args = ["design", "action", action.id]
  if (bar && bar.target) {
    args.push("--target")
    args.push(String(bar.target))
  }
  return args
}

function chipArgs(bar, chip) {
  if (chip.action === "ask")
    return askArgs(bar, chip.prompt)
  if (chip.action === "sendDesk")
    return sendArgs(bar, "desk", "")
  return actionArgs(bar, { id: chip.action })
}

function askArgs(bar, prompt) {
  var args = ["design", "ask"]
  if (bar && bar.target) {
    args.push("--target")
    args.push(String(bar.target))
  }
  return args.concat(String(prompt).split(/\s+/).filter(function (word) { return word.length > 0 }))
}

function sendArgs(bar, destination, prompt) {
  var args = ["design", "send", destination]
  if (bar && bar.target) {
    args.push("--target")
    args.push(String(bar.target))
  } else if (bar && bar.surface) {
    args.push("--surface")
    args.push(bar.surface)
  }
  if (prompt) {
    args.push("--prompt")
    args.push(prompt)
  }
  return args
}

// Desktop Look's gap handle (docs/ANYWHERE.md, phase 4): the gap on a window's right. At the monitor's edge it's
// the outer gap; between windows it's twice the inner gap. Null unless Desktop Look shows handles. Rectangles are
// the overlay's own coordinates on `screen`.
function gapHandle(look, bounds, screen) {
  if (!look || !look.handles || !bounds)
    return null
  var box = local(bounds, screen)
  var right = box.x + box.width
  var outer = right + look.gapsOut + (look.borderSize || 0) >= screen.width - 2
  var width = outer ? look.gapsOut : look.gapsIn * 2
  return { key: outer ? "gapsOut" : "gapsIn", label: outer ? "Outer gap" : "Inner gap",
           value: outer ? look.gapsOut : look.gapsIn, x: right, y: box.y, width: width, height: box.height }
}

// The gap a handle dragged by `dx` pixels asks for, within Hyprland's sensible range.
function gapAfterDrag(handle, dx) {
  var width = Math.max(0, handle.width + dx)
  var value = handle.key === "gapsOut" ? width : width / 2
  return Math.max(0, Math.min(200, Math.round(value)))
}

// The command a released gap handle runs: a live preview through `omastrator design look`.
function gapArgs(handle, value) {
  return ["design", "look", handle.key + "=" + value]
}

// A lift's progress on the bar: "Lifting div.card: Fetching pictures… 3 of 8".
function liftText(lift) {
  if (!lift) return ""
  var text = "Lifting " + lift.label + ": " + lift.stage
  if (lift.total > 0)
    text += " " + lift.done + " of " + lift.total
  return text
}
