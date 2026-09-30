#pragma once
#include <QByteArray>

// The key files `omastrator setup` wrote before the safety round (a Lua file with plain hl.bind and hl.define_submap, an undescribed
// dictation release bind and a reset key that isn't universal; a hyprlang file with one reset bind and no leading
// `submap = reset`), with the command "omastrator". Kept as raw text so an upgrade is tested against what people really have.
namespace OldKeyFiles {
inline const QByteArray lua = R"KEYS(-- Omastrator's island keys, written by `omastrator setup` (docs/OS-SUITE.md).
-- Super+Alt+D, C, A or L switches the island to Draw, Capture, AI or Live and
-- takes that mode's keys. Escape goes back to Normal. While a mode's keys are
-- held, other shortcuts wait until Escape; unbound keys still reach apps.
-- `omastrator setup` rewrites this file; `omastrator setup --remove` deletes it.

local omastrator = "omastrator"

local function island(args)
  return hl.dsp.exec_cmd(omastrator .. " island " .. args)
end

local function enter(mode)
  return function()
    hl.dispatch(island("mode " .. mode))
    hl.dispatch(hl.dsp.submap("omastrator-" .. mode))
  end
end

-- Runs one island command, then hands the keyboard back.
local function leave(args)
  return function()
    hl.dispatch(island(args))
    hl.dispatch(hl.dsp.submap("reset"))
  end
end

hl.bind("SUPER + ALT + D", enter("draw"), { description = "Omastrator: Draw mode" })
hl.bind("SUPER + ALT + C", enter("capture"), { description = "Omastrator: Capture mode" })
hl.bind("SUPER + ALT + A", enter("ai"), { description = "Omastrator: AI mode" })
hl.bind("SUPER + ALT + L", enter("live"), { description = "Omastrator: Live mode" })

-- Dictation: hold Super+Alt+V and speak; release to hear it back. Esc cancels while it waits.
hl.bind("SUPER + ALT + V", island("dictate start"), { description = "Omastrator: dictate (hold)" })
hl.bind("SUPER + ALT + V", island("dictate stop"), { release = true })
hl.define_submap("omastrator-heard", function()
  hl.bind("Escape", leave("dictate cancel"), { description = "Cancel what was heard" })
end)

hl.define_submap("omastrator-draw", function()
  hl.bind("V", island("tool select"), { description = "Selection" })
  hl.bind("A", island("tool directSelect"), { description = "Direct Selection" })
  hl.bind("P", island("tool pen"), { description = "Pen" })
  hl.bind("N", island("tool pencil"), { description = "Pencil" })
  hl.bind("M", island("tool rectangle"), { description = "Rectangle" })
  hl.bind("L", island("tool ellipse"), { description = "Ellipse" })
  hl.bind("backslash", island("tool line"), { description = "Line Segment" })
  hl.bind("I", island("tool eyedropper"), { description = "Eyedropper" })
  hl.bind("H", island("tool hand"), { description = "Hand" })
  hl.bind("Z", island("tool zoom"), { description = "Zoom" })
  -- Type takes letters, so choosing it hands the keyboard back.
  hl.bind("T", leave("tool text"), { description = "Type" })
  hl.bind("Escape", leave("mode normal"), { description = "Back to Normal" })
end)

hl.define_submap("omastrator-capture", function()
  hl.bind("F", leave("capture color fill"), { description = "Pick colour for fill" })
  hl.bind("S", leave("capture color stroke"), { description = "Pick colour for stroke" })
  hl.bind("W", leave("capture color swatch"), { description = "Pick colour as a swatch" })
  hl.bind("R", leave("capture screenshot"), { description = "Screenshot region" })
  hl.bind("A", leave("capture window"), { description = "Capture the focused window" })
  hl.bind("V", leave("capture paste-svg"), { description = "Paste SVG" })
  hl.bind("T", leave("capture theme-swatches"), { description = "Theme swatches" })
  hl.bind("Escape", leave("mode normal"), { description = "Back to Normal" })
end)

hl.define_submap("omastrator-ai", function()
  hl.bind("G", leave("ai generate"), { description = "Generate" })
  hl.bind("E", leave("ai edit"), { description = "Edit with Instruction" })
  hl.bind("R", leave("ai roast"), { description = "Roast My Design" })
  hl.bind("V", leave("ai vectorize"), { description = "Vectorize with AI" })
  hl.bind("Escape", leave("mode normal"), { description = "Back to Normal" })
end)

hl.define_submap("omastrator-live", function()
  hl.bind("O", leave("live start"), { description = "Open a page…" })
  hl.bind("D", leave("live deploy"), { description = "Deploy" })
  hl.bind("S", leave("live save"), { description = "Save" })
  hl.bind("R", leave("live changes"), { description = "Review changes" })
  hl.bind("H", leave("live history"), { description = "History" })
  hl.bind("X", leave("live stop"), { description = "Stop Live" })
  hl.bind("Escape", leave("mode normal"), { description = "Back to Normal" })
end)

-- Design mode everywhere (docs/ANYWHERE.md): SUPER + ALT + O inspects, measures and draws on any
-- window or page; clicks still reach the apps. Hold Alt to measure; Escape leaves.
-- SUPER + ALT + W opens the Desk on its own workspace. Remap both with "keys" in
-- ~/.config/omastrator/anywhere.json, then run `omastrator setup` again.
local function design(args)
  return hl.dsp.exec_cmd(omastrator .. " design " .. args)
end

local function leaveDesign()
  hl.dispatch(design("off"))
  hl.dispatch(hl.dsp.submap("reset"))
end

hl.bind("SUPER + ALT + O", function()
  hl.dispatch(design("on"))
  hl.dispatch(hl.dsp.submap("omastrator-design"))
end, { description = "Omastrator: design mode" })
hl.bind("SUPER + ALT + W", hl.dsp.exec_cmd(omastrator .. " desk toggle"), { description = "Omastrator: the Desk" })
-- The escape hatch: ends design mode, drops previews and gives the keyboard back, from anywhere.
hl.bind("SUPER + ALT + Escape", hl.dsp.exec_cmd(omastrator .. " reset"), { description = "Omastrator: reset" })
hl.define_submap("omastrator-design", function()
  hl.bind("Escape", leaveDesign, { description = "Leave design mode" })
  hl.bind("SUPER + ALT + O", leaveDesign, { description = "Leave design mode" })
  hl.bind("Alt_L", design("alt on"), { description = "Measure (hold)" })
  hl.bind("ALT + Alt_L", design("alt off"), { release = true })
end)

-- Omastrator in the background: the overlays, the Desk and the agent socket, no window until asked.
hl.on("hyprland.start", function()
  hl.exec_cmd(omastrator .. " --daemon")
end)
)KEYS";

inline const QByteArray conf = R"KEYS(# Omastrator's island keys, written by `omastrator setup` (docs/OS-SUITE.md).
# Super+Alt+D, C, A or L switches the island's mode and takes its keys; Escape goes back.

bindd = SUPER ALT, D, Omastrator: Draw mode, exec, omastrator island mode draw
bind = SUPER ALT, D, submap, omastrator-draw
bindd = SUPER ALT, C, Omastrator: Capture mode, exec, omastrator island mode capture
bind = SUPER ALT, C, submap, omastrator-capture
bindd = SUPER ALT, A, Omastrator: AI mode, exec, omastrator island mode ai
bind = SUPER ALT, A, submap, omastrator-ai
bindd = SUPER ALT, L, Omastrator: Live mode, exec, omastrator island mode live
bind = SUPER ALT, L, submap, omastrator-live

bindd = SUPER ALT, V, Omastrator: dictate (hold), exec, omastrator island dictate start
bindr = SUPER ALT, V, exec, omastrator island dictate stop

submap = omastrator-heard
bind = , escape, exec, omastrator island dictate cancel
bind = , escape, submap, reset
submap = reset

submap = omastrator-draw
bind = , V, exec, omastrator island tool select
bind = , A, exec, omastrator island tool directSelect
bind = , P, exec, omastrator island tool pen
bind = , N, exec, omastrator island tool pencil
bind = , M, exec, omastrator island tool rectangle
bind = , L, exec, omastrator island tool ellipse
bind = , backslash, exec, omastrator island tool line
bind = , I, exec, omastrator island tool eyedropper
bind = , H, exec, omastrator island tool hand
bind = , Z, exec, omastrator island tool zoom
bind = , T, exec, omastrator island tool text
bind = , T, submap, reset
bind = , escape, exec, omastrator island mode normal
bind = , escape, submap, reset
submap = reset

submap = omastrator-capture
bind = , F, exec, omastrator island capture color fill
bind = , F, submap, reset
bind = , S, exec, omastrator island capture color stroke
bind = , S, submap, reset
bind = , W, exec, omastrator island capture color swatch
bind = , W, submap, reset
bind = , R, exec, omastrator island capture screenshot
bind = , R, submap, reset
bind = , A, exec, omastrator island capture window
bind = , A, submap, reset
bind = , V, exec, omastrator island capture paste-svg
bind = , V, submap, reset
bind = , T, exec, omastrator island capture theme-swatches
bind = , T, submap, reset
bind = , escape, exec, omastrator island mode normal
bind = , escape, submap, reset
submap = reset

submap = omastrator-ai
bind = , G, exec, omastrator island ai generate
bind = , G, submap, reset
bind = , E, exec, omastrator island ai edit
bind = , E, submap, reset
bind = , R, exec, omastrator island ai roast
bind = , R, submap, reset
bind = , V, exec, omastrator island ai vectorize
bind = , V, submap, reset
bind = , escape, exec, omastrator island mode normal
bind = , escape, submap, reset
submap = reset

submap = omastrator-live
bind = , O, exec, omastrator island live start
bind = , O, submap, reset
bind = , D, exec, omastrator island live deploy
bind = , D, submap, reset
bind = , S, exec, omastrator island live save
bind = , S, submap, reset
bind = , R, exec, omastrator island live changes
bind = , R, submap, reset
bind = , H, exec, omastrator island live history
bind = , H, submap, reset
bind = , X, exec, omastrator island live stop
bind = , X, submap, reset
bind = , escape, exec, omastrator island mode normal
bind = , escape, submap, reset
submap = reset

bindd = SUPER ALT, O, Omastrator: design mode, exec, omastrator design on
bind = SUPER ALT, O, submap, omastrator-design
bindd = SUPER ALT, W, Omastrator: the Desk, exec, omastrator desk toggle
bindd = SUPER ALT, escape, Omastrator: reset, exec, omastrator reset

submap = omastrator-design
bind = , escape, exec, omastrator design off
bind = , escape, submap, reset
bind = SUPER ALT, O, exec, omastrator design off
bind = SUPER ALT, O, submap, reset
bind = , Alt_L, exec, omastrator design alt on
bindr = ALT, Alt_L, exec, omastrator design alt off
submap = reset

exec-once = omastrator --daemon
)KEYS";

// The line the old `--apply` added to the user's Hyprland config.
inline const QByteArray luaSource = "\n-- Omastrator's island keys (added by `omastrator setup --apply`).\n"
                                    "pcall(dofile, (os.getenv(\"XDG_CONFIG_HOME\") or (os.getenv(\"HOME\") .. \"/.config\")) .. \"/omastrator/hyprland.lua\")\n";
}

// The key files of the last version that still had the desktop island (Draw, Capture, AI and Live modes, four submaps), as that
// version's setup wrote them. An upgrade to the version without the island is tested against them.
namespace IslandKeyFiles {
inline const QByteArray lua = R"KEYS(-- Omastrator's island keys, written by `omastrator setup` (docs/OS-SUITE.md).
-- Super+Alt+D, C, A or L switches the island to Draw, Capture, AI or Live and
-- takes that mode's keys. Escape goes back to Normal. While a mode's keys are
-- held, other shortcuts wait until Escape; unbound keys still reach apps.
-- `omastrator setup` rewrites this file; `omastrator setup --remove` deletes it.

local omastrator = "omastrator"

-- A failure in this file is written to ~/.local/state/omastrator/setup.log and shown as one
-- notification once the file has loaded. It never stops the lines after it or the user's own config.
local failures, loading = {}, true

local function notify(message)
  pcall(hl.notification.create, { text = message, timeout = 10000 })
end

local function report(what, err)
  local message = "Omastrator: " .. what .. ": " .. tostring(err)
  pcall(function()
    local state = os.getenv("XDG_STATE_HOME") or ((os.getenv("HOME") or "") .. "/.local/state")
    local path = state .. "/omastrator/setup.log"
    local log = io.open(path, "a")
    -- Every reload appends, so start over once it is large.
    if log and log:seek("end") > 65536 then log:close(); log = io.open(path, "w") end
    if log then
      log:write(os.date("%Y-%m-%d %H:%M:%S "), message, "\n")
      log:close()
    end
  end)
  if loading then failures[#failures + 1] = message else notify(message) end
end

local function attempt(what, fn, ...)
  local ok, err = pcall(fn, ...)
  if not ok then report(what, err) end
  return ok
end

local function bind(keys, action, options)
  attempt("couldn't bind " .. keys, hl.bind, keys, action, options)
end

-- Hyprland closes a submap when its body ends or fails; the body is guarded too so the failure is reported.
local function submap(name, body)
  attempt("submap " .. name, hl.define_submap, name, function()
    attempt("submap " .. name, body)
  end)
end

local function run(action)
  attempt("a key failed", hl.dispatch, action)
end

local function island(args)
  return hl.dsp.exec_cmd(omastrator .. " island " .. args)
end

local function enter(mode)
  return function()
    run(island("mode " .. mode))
    run(hl.dsp.submap("omastrator-" .. mode))
  end
end

-- Hands the keyboard back first, so a failing command can't leave the mode's keys held; then runs one island command.
local function leave(args)
  return function()
    run(hl.dsp.submap("reset"))
    run(island(args))
  end
end

bind("SUPER + ALT + D", enter("draw"), { description = "Omastrator: Draw mode" })
bind("SUPER + ALT + C", enter("capture"), { description = "Omastrator: Capture mode" })
bind("SUPER + ALT + A", enter("ai"), { description = "Omastrator: AI mode" })
bind("SUPER + ALT + L", enter("live"), { description = "Omastrator: Live mode" })

-- Dictation: hold Super+Alt+V and speak; release to hear it back. Esc cancels while it waits.
bind("SUPER + ALT + V", island("dictate start"), { description = "Omastrator: dictate (hold)" })
bind("SUPER + ALT + V", island("dictate stop"), { release = true, description = "Omastrator: dictate (release)" })
submap("omastrator-heard", function()
  bind("Escape", leave("dictate cancel"), { description = "Cancel what was heard" })
end)

submap("omastrator-draw", function()
  bind("V", island("tool select"), { description = "Selection" })
  bind("A", island("tool directSelect"), { description = "Direct Selection" })
  bind("P", island("tool pen"), { description = "Pen" })
  bind("N", island("tool pencil"), { description = "Pencil" })
  bind("M", island("tool rectangle"), { description = "Rectangle" })
  bind("L", island("tool ellipse"), { description = "Ellipse" })
  bind("backslash", island("tool line"), { description = "Line Segment" })
  bind("I", island("tool eyedropper"), { description = "Eyedropper" })
  bind("H", island("tool hand"), { description = "Hand" })
  bind("Z", island("tool zoom"), { description = "Zoom" })
  -- Type takes letters, so choosing it hands the keyboard back.
  bind("T", leave("tool text"), { description = "Type" })
  bind("Escape", leave("mode normal"), { description = "Back to Normal" })
end)

submap("omastrator-capture", function()
  bind("F", leave("capture color fill"), { description = "Pick colour for fill" })
  bind("S", leave("capture color stroke"), { description = "Pick colour for stroke" })
  bind("W", leave("capture color swatch"), { description = "Pick colour as a swatch" })
  bind("R", leave("capture screenshot"), { description = "Screenshot region" })
  bind("A", leave("capture window"), { description = "Capture the focused window" })
  bind("V", leave("capture paste-svg"), { description = "Paste SVG" })
  bind("T", leave("capture theme-swatches"), { description = "Theme swatches" })
  bind("Escape", leave("mode normal"), { description = "Back to Normal" })
end)

submap("omastrator-ai", function()
  bind("G", leave("ai generate"), { description = "Generate" })
  bind("E", leave("ai edit"), { description = "Edit with Instruction" })
  bind("R", leave("ai roast"), { description = "Roast My Design" })
  bind("V", leave("ai vectorize"), { description = "Vectorize with AI" })
  bind("Escape", leave("mode normal"), { description = "Back to Normal" })
end)

submap("omastrator-live", function()
  bind("O", leave("live start"), { description = "Open a page…" })
  bind("D", leave("live deploy"), { description = "Deploy" })
  bind("S", leave("live save"), { description = "Save" })
  bind("R", leave("live changes"), { description = "Review changes" })
  bind("H", leave("live history"), { description = "History" })
  bind("X", leave("live stop"), { description = "Stop Live" })
  bind("Escape", leave("mode normal"), { description = "Back to Normal" })
end)

-- Design mode everywhere (docs/ANYWHERE.md): SUPER + ALT + O inspects, measures and draws on any
-- window or page; clicks still reach the apps. Hold Alt to measure; Escape leaves.
-- SUPER + ALT + W opens the Desk on its own workspace. Remap both with "keys" in
-- ~/.config/omastrator/anywhere.json, then run `omastrator setup` again.
local function design(args)
  return hl.dsp.exec_cmd(omastrator .. " design " .. args)
end

local function leaveDesign()
  run(hl.dsp.submap("reset"))
  run(design("off"))
end

bind("SUPER + ALT + O", function()
  run(design("on"))
  run(hl.dsp.submap("omastrator-design"))
end, { description = "Omastrator: design mode" })
bind("SUPER + ALT + W", hl.dsp.exec_cmd(omastrator .. " desk toggle"), { description = "Omastrator: the Desk" })
-- The escape hatch: gives the keyboard back, ends design mode and drops previews. It works inside any
-- submap (submap_universal) and closes the submap itself, so it needs neither the app nor the island.
bind("SUPER + ALT + Escape", function()
  run(hl.dsp.submap("reset"))
  run(hl.dsp.exec_cmd(omastrator .. " reset"))
end, { description = "Omastrator: reset", submap_universal = true })
submap("omastrator-design", function()
  bind("Escape", leaveDesign, { description = "Leave design mode" })
  bind("SUPER + ALT + O", leaveDesign, { description = "Leave design mode" })
  bind("Alt_L", design("alt on"), { description = "Measure (hold)" })
  bind("ALT + Alt_L", design("alt off"), { release = true, description = "Measure (release)" })
end)

-- Omastrator in the background: the overlays, the Desk and the agent socket, no window until asked.
attempt("hyprland.start", hl.on, "hyprland.start", function()
  hl.exec_cmd(omastrator .. " --daemon")
end)

loading = false
if #failures > 0 then
  notify("Omastrator: " .. #failures .. " of its keys didn't load (details in ~/.local/state/omastrator/setup.log). First: " .. failures[1])
end
)KEYS";

inline const QByteArray conf = R"KEYS(# Omastrator's island keys, written by `omastrator setup` (docs/OS-SUITE.md).
# Super+Alt+D, C, A or L switches the island's mode and takes its keys; Escape goes back.
# Every mode's keys sit in a submap that ends with `submap = reset`. This first line makes sure nothing
# above this file (an unclosed submap in the user's own config) holds the binds below.
submap = reset

bindd = SUPER ALT, D, Omastrator: Draw mode, exec, omastrator island mode draw
bind = SUPER ALT, D, submap, omastrator-draw
bindd = SUPER ALT, C, Omastrator: Capture mode, exec, omastrator island mode capture
bind = SUPER ALT, C, submap, omastrator-capture
bindd = SUPER ALT, A, Omastrator: AI mode, exec, omastrator island mode ai
bind = SUPER ALT, A, submap, omastrator-ai
bindd = SUPER ALT, L, Omastrator: Live mode, exec, omastrator island mode live
bind = SUPER ALT, L, submap, omastrator-live

bindd = SUPER ALT, V, Omastrator: dictate (hold), exec, omastrator island dictate start
bindr = SUPER ALT, V, exec, omastrator island dictate stop

submap = omastrator-heard
bind = , escape, exec, omastrator island dictate cancel
bind = , escape, submap, reset
submap = reset

submap = omastrator-draw
bind = , V, exec, omastrator island tool select
bind = , A, exec, omastrator island tool directSelect
bind = , P, exec, omastrator island tool pen
bind = , N, exec, omastrator island tool pencil
bind = , M, exec, omastrator island tool rectangle
bind = , L, exec, omastrator island tool ellipse
bind = , backslash, exec, omastrator island tool line
bind = , I, exec, omastrator island tool eyedropper
bind = , H, exec, omastrator island tool hand
bind = , Z, exec, omastrator island tool zoom
bind = , T, exec, omastrator island tool text
bind = , T, submap, reset
bind = , escape, exec, omastrator island mode normal
bind = , escape, submap, reset
submap = reset

submap = omastrator-capture
bind = , F, exec, omastrator island capture color fill
bind = , F, submap, reset
bind = , S, exec, omastrator island capture color stroke
bind = , S, submap, reset
bind = , W, exec, omastrator island capture color swatch
bind = , W, submap, reset
bind = , R, exec, omastrator island capture screenshot
bind = , R, submap, reset
bind = , A, exec, omastrator island capture window
bind = , A, submap, reset
bind = , V, exec, omastrator island capture paste-svg
bind = , V, submap, reset
bind = , T, exec, omastrator island capture theme-swatches
bind = , T, submap, reset
bind = , escape, exec, omastrator island mode normal
bind = , escape, submap, reset
submap = reset

submap = omastrator-ai
bind = , G, exec, omastrator island ai generate
bind = , G, submap, reset
bind = , E, exec, omastrator island ai edit
bind = , E, submap, reset
bind = , R, exec, omastrator island ai roast
bind = , R, submap, reset
bind = , V, exec, omastrator island ai vectorize
bind = , V, submap, reset
bind = , escape, exec, omastrator island mode normal
bind = , escape, submap, reset
submap = reset

submap = omastrator-live
bind = , O, exec, omastrator island live start
bind = , O, submap, reset
bind = , D, exec, omastrator island live deploy
bind = , D, submap, reset
bind = , S, exec, omastrator island live save
bind = , S, submap, reset
bind = , R, exec, omastrator island live changes
bind = , R, submap, reset
bind = , H, exec, omastrator island live history
bind = , H, submap, reset
bind = , X, exec, omastrator island live stop
bind = , X, submap, reset
bind = , escape, exec, omastrator island mode normal
bind = , escape, submap, reset
submap = reset

bindd = SUPER ALT, O, Omastrator: design mode, exec, omastrator design on
bind = SUPER ALT, O, submap, omastrator-design
bindd = SUPER ALT, W, Omastrator: the Desk, exec, omastrator desk toggle
binddu = SUPER ALT, escape, Omastrator: reset, exec, omastrator reset
binddu = SUPER ALT, escape, Omastrator: reset, submap, reset

submap = omastrator-design
bind = , escape, exec, omastrator design off
bind = , escape, submap, reset
bind = SUPER ALT, O, exec, omastrator design off
bind = SUPER ALT, O, submap, reset
bind = , Alt_L, exec, omastrator design alt on
bindr = ALT, Alt_L, exec, omastrator design alt off
submap = reset

exec-once = omastrator --daemon
)KEYS";
}
