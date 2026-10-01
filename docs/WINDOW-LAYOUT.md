# The window layout: the island moves into the window (2026-09-29)

Decided with the author on 2026-09-29. **This changes ANYWHERE.md's premise** ("there's no window
by default; a design layer over the whole desktop"): Omastrator is now an app first. The desktop
island goes, and every workflow it started gets a place in the window. Read with VISION.md.

## The stack, top to bottom

```
┌ File  Edit  Object  Type  Select  View  Window  Help ─────────────┐
├ + [document tabs…]                     cloud · zoom · Share ──────┤
├ selection context · AI status · [Ask… 🎤] ────────────────────────┤
├ island: the current tool's options ───────────────────────────────┤
├──┬──────────┬───────────────────────────────┬─────────────────────┤
│to│ Layers   │                               │ Properties│Capture  │
│ol│ (Pages)  │            canvas             │                     │
│s │          │                               │                     │
└──┴──────────┴───────────────────────────────┴─────────────────────┘
```

1. **Menu bar**, as today.
2. **Document tabs**, a row of their own (cloud status, zoom and Share stay at its right end).
3. **Selection context / AI status**: what's selected, the agent's state (working, a proposal
   waiting, with Keep/Discard), and an **Ask** field with a mic.
4. **The island**: a full-width bar with the current tool's options. It changes with the tool on
   the left rail. The Frame tool shows frame options, and a frame with Browser View on shows the
   Live controls (page, element, Changes, History, Deploy). It replaces today's per-tool header
   (`ToolHeaderBar`).
5. **Left: the tool rail, then Layers** (with Pages), as Figma has it.
6. **Right: a dock with tabs, Properties and Capture.**

## Tools
- **Frame (F) does Browser View.** There's no separate Browser View tool. A frame has a Browser
  View switch on the canvas beside it (and in the island bar):
  - **On:** starts the project's localhost server with the live code, and the frame shows the page.
  - **Off:** the server is frozen (paused, not stopped) until it's turned on again; the frame keeps
    its last picture.
  - **Closing the app** shuts every server down.
- **Artboards** are going (decided; parked in `docs/backlog/FRAMES-ONLY.md`). Until then the
  Artboard tool keeps its own rail slot.

## Where the desktop island's workflows go
- **Capture** (colour from the screen, a region, a window, paste SVG): the **Capture tab** in the
  right dock. Action buttons on top (Region, Colour, Paste SVG), the open windows as a grid of
  thumbnails (click one to bring it onto the canvas), recent colours at the bottom. Results land on
  the canvas, in the selected frame if there is one.
- **Dictation:** the **mic in the Ask field** (row 3). Hold it, or Super+Alt+V while the window is
  focused, and speak: the words fill the field. Tier-1 commands (align, zoom, colours…) run at
  once; anything else waits for Enter, so it can be corrected first.
- **Design mode** (inspect, measure, draw, lift on other apps and sites): **"Design over…"** in
  the island bar and Ctrl+K picks an open window or a site. It lands on the canvas in a frame (a
  site as a live Browser View, an app as a live window picture), where the normal tools inspect,
  measure, draw and lift. No overlay on the desktop.

## Phases
- **Phase A (before the announcement video):** frame resize never scales text; the stack above
  with Layers on the left and the Properties/Capture tabs; the Ask field and mic; the island bar
  by tool; Frame's Browser View switch and the server lifecycle. Capture's action buttons work, and its
  window thumbnails (phase B, done).
- **Phase B:** Design over…; removing the desktop island plugin, its
  Hyprland keys and submaps, and its setup steps.

## Decided without the author (review)

The author asked for these to be decided and logged overnight (2026-09-29/30). Each can be changed.

- [Mac mini] **The island is the tool's bar.** Row 4 is the per-tool bar that existed (`ToolHeaderBar`), moved under
  the selection/AI row; each tool shows its own options there.
- [Mac mini] **The agent's state lives in row 3.** Waiting, then Keep/Discard/Cancel (`ProposalBar`) moved there from
  above the canvas.
- [Mac mini] **Ask** runs Edit with Instruction on the selection (the whole document when nothing is selected). One
  request at a time: Ask and the mic are off while an agent works, while a proposal waits, and without a document.
- [Mac mini] **The mic is push-to-talk.** Hold it; release transcribes. A local-grammar command ("rectangle tool",
  "align left") runs at once, with no confirm delay; anything else fills Ask and waits for Enter. Without an icon
  theme's microphone, the button reads "Mic".
- [Mac mini] **Capture tab, phase A:** Region, Fill Colour, Stroke Colour, Swatch, Paste SVG, Theme Swatches. A region
  or a colour first switches to the previous workspace (the window usually covers what's wanted) and comes back after.
  Capturing a whole window is the thumbnail grid below, since "the focused window" is now Omastrator.
- [Mac mini] **Capture tab, window thumbnails:** under the buttons, a 2-column grid of the open windows (from
  `hyprctl clients`; not Omastrator's own, hidden or unmapped ones, or special-workspace ones that aren't showing),
  most recently used first, each a thumbnail and an elided "app · title". Windows on screen get a `grim` picture of
  their geometry, taken off the UI thread; the others show the app's icon or a lettered tile. The grid is read when
  the tab shows and by its Refresh button, never polled. Clicking a window goes to its workspace (or just focuses
  it, when it is on screen), waits 250 ms for it to draw, grabs it with `grim`, returns to the previous workspace
  (or refocuses Omastrator), then runs `omastrator island capture image <png>`, which opens and traces it like
  the other captures. Recent colours are left out.
- [Mac mini] **The Frame tool's island:** "Drop a size…" (the built-in presets; drops a frame in the middle of the view
  and switches to Selection, as Figma does), Clip content and Add/Remove Auto Layout for the selected frames, and a
  short hint.
- [Mac mini] **Side panels:** Layers is 240 px by default, 220–400 px by dragging its edge, remembered. Window ▸ Layers
  hides the left panel; Window ▸ Properties hides the whole right dock, Capture included.
- [Mac mini] **Row 3's selection text:** "Hero · 480 × 320" for one object (its name, else its kind), "3 objects", or
  "Nothing selected".
- [Mac mini] **Suggested, not done yet:** remove the floating task bar's own "Ask AI…" field (row 3 has Ask now) and
  the Frame presets list in Properties while the Frame tool is on (the island has "Drop a size…").
- [Intel] **Fix with <agent> runs in the project's own folder, not a worktree.** A worktree has no `node_modules` or
  `.env`, so the agent couldn't run the build to check its fix. What it changed is read from git afterwards and kept as
  a "Fix" write-back record (Review Changes, Discard), so **Deploy again** commits it; the agent itself never commits,
  pushes or deploys.
- [Intel] **The fix lives in Deploy Details.** The button, its running state with Stop, the agent's summary with the
  files it changed, and **Deploy again** are in the window that opens from "Details" and from a failed frame's bar. They
  show for failed production deploys only (not a failed Save or a Share preview). With no default agent the button is
  hidden and the label shows Omarchy's hint to choose one. One fix runs at a time, and only while no other agent task is
  waiting.
- [Intel] **A stopped or silent run is still read.** If the agent is stopped or exits without an answer, the window says
  so and lists any files it wrote; they are recorded like a finished fix, so nothing is left uncommitted without a way to
  discard it.
- [Intel] **The failure line** now skips box borders and update banners, prefers a JSON `message` (with its `reason`),
  and prefers an "error" line over a later plain one, in every Deploy step (push, GitHub, the deploy command).
- [Mac mini] **Browser View switch: opening a file starts no server.** A view saved on shows its page (and last
  picture); its project's dev server starts only when it's switched on, or on the first Edit Page. No `npm install` on
  open.
- [Mac mini] **An off frame resizes as a plain frame** (a real, undoable resize); it keeps its address and last picture
  and wears the plain frame label.
- [Mac mini] **Where the switch sits on the canvas:** a Browser View has it in its bar after back, forward and reload;
  any other frame, and an off view, shows a "Browser View" pill at its top-right while it's selected or hovered.
- [Mac mini] **Closing the last window in daemon mode stops the servers** (`BrowserViews::stopServers()`), since to the
  user the app has closed; a frame still switched on starts its server again when its canvas is next seen.
- [Mac mini] **The old Browser View tool name maps to Frame:** `Tool::browserView` still reads (files, settings,
  scripts) but picks Frame, and it's gone from the rail and Ctrl+K.
- [Mac mini] **Frozen = SIGSTOP of the server's process group,** so it keeps its port, state and PID; on again is
  SIGCONT. Quitting wakes a frozen group before SIGTERM.
- [Mac mini] **The Frame island's Browser View controls:** a "Browser View" switch for the single selected frame (off
  with none or several; the same flip as the pill, one undo step). While it's on, the hint gives way to a Live group:
  the server state ("No server", "Starting…", "Running", "Frozen", "Failed", or "Type a URL" with no page yet), Edit
  Page, Reload, Changes, History, Deploy and Stop Live, each running the same host action as Object ▸ Browser View.
  Keep Edits, Build It and the rest stay in the bar menu and Object ▸ Browser View.
- [Mac mini] **Frames on the canvas follow Figma** (the frame text fix, 81d6bee): resizing a frame never scales
  what's inside (children keep their size and follow their constraints; area text gets a wider box, point text
  keeps its size), including imported, flipped and grouped cases. Objects drawn with the shape, Pen, Pencil and
  Type tools go into the topmost frame under the press; a Selection-tool move that ends over a frame drops the
  object into it (and out, when it ends outside every frame), in the same undo step. Dragging a whole frame onto
  another frame nests it. The Scale tool still scales everything. Rotated frames and group-based instances still
  scale as a whole (not changed).
- [Intel] **The desktop island is removed** (Phase B, `d76cafb`). Decided while removing it:
  - Design mode keeps a menu entry: the Omarchy menu has "Design Mode" (`omastrator island mode design`) beside "The Desk", instead of inside a submenu. The brief removed "Island Mode ▸" only.
  - Design mode's drawing tools (pen, rectangle, …, Undo, Clear, Desk, Onboarding, Done) were buttons on the pill, so they have no mouse control now. They are `omastrator design tool …`, `design undo`, `design clear all`, `desk show`, `design onboarding open`, `design off`. Esc still leaves (under a drawing tool the overlay takes the keyboard). "Design over…" is the fix; I added no strip to the overlay.
  - Design mode's own lines (turning on, its messages, a proposal on the overlay) are notified by `Design.qml` through `notify-send`, because the pill used to show them and nothing else would.
  - Notifications share the hint `x-canonical-private-synchronous:omastrator`, so "Listening…", "Transcribing…" and "Heard: …" replace one card where the daemon honours it (mako and dunst do). Empty text sends nothing.
  - The activity seconds are the notification timeout, so "Listening…" (60 s) stays until the next line replaces it.
  - Setup also deletes `~/.local/state/omastrator/island-seen.json` (the pill's first-use labels). The brief did not list it.
  - The setup step for `shell.json` is named `design`; a `--remove` on a record written by the old version still works.
  - `mode design` starts the background app but no window (`mode draw` used to show the window).
- [Intel] **A page dot goes through the app** (`omastrator island page <workspace>`, `go_to_page`). The app takes the user along whatever has focus, because a bar click is an explicit request; Alt+PageDown follows only with focus. When the app can't (not running, feature off), the dot runs Hyprland's focus as before. The dot on the page that is already current only focuses its workspace.
- [Intel] **The Hyprland path (Super+Tab, a swipe) still shows the stand-in for about 50 ms plus the swap,** and its fade stays: the user is looking at the workspace when the editor arrives, and the fade is Hyprland's own animation, which the app does not change. The editor now paints the new page before it moves in. The 50 ms coalesce stays, since a shorter one would move the editor to every workspace of a fast run.
  - Dictation no longer routes requests to `live ask` when Live mode was on; every tier-2 request goes to Edit with Instruction.
  - README's three island screenshots (`island-modes.png`, `island-activity.png`, `island-dictation.png`) are deleted, and the "Across Omarchy" section describes the window as the home of Draw, AI, Capture and Live.
  - The empty Swatches panel now says "Pick a colour … with the Capture tab".
  - Text that told people to use the island now names a command: "stop it with `omastrator island ai cancel`", "`omastrator design undo` brings them back", "Start it from a Browser View, or with `omastrator island live start`".
  - The name `omastrator island …` and the `Island` C++ namespace stay: they name the desktop CLI and the mode file. Internal names such as `fromIsland` in the deploy code stay too.
- [Intel] **Page dots in the Omarchy bar** (`omastrator.pages`). Decided while building them:
  - It is a plugin of its own, not a second widget of `omastrator.ai`: a manifest declares one bar widget.
  - Setup offers it as a separate step (`pages`), not with the tray light's question: the light is optional, and the dots do nothing (zero width) until a document claims workspaces. An old install gets it on the next `omastrator setup`. Its record flag is `shellJson.pages`, so `--remove` takes out only what setup added.
  - With no bar layout in `shell.json` setup adds nothing and says so. Adding `left` would replace the shell's default layout (menu, workspaces, clock…) with one dot.
  - The stream shows pages only while an app is running, so a `workspaces.json` left by a crash shows no dots.
  - The tooltip is the page's name; with two documents, "Page (Document)". No separator between documents.
  - Every page button has the same opacity rule as the numbers, focused 1 and the rest 0.5. A page workspace always holds the editor or a stand-in, so there is no "empty" state.
  - The claims file gained `document` and `pageName` for each claim; older files are read by splitting the workspace name at the first " · ".
  - The gap before the first page is `Style.spaceReal(3)`, about twice the trailing gap of Omarchy's numbers. I did not draw a separator line.
