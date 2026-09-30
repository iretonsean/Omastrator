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
  by tool; Frame's Browser View switch and the server lifecycle. Capture's action buttons work.
- **Phase B:** Design over…; Capture's window thumbnails; removing the desktop island plugin, its
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
  Capturing a whole window waits for the window thumbnails (phase B), since "the focused window" is now Omastrator.
- [Mac mini] **The Frame tool's island:** "Drop a size…" (the built-in presets; drops a frame in the middle of the view
  and switches to Selection, as Figma does), Clip content and Add/Remove Auto Layout for the selected frames, and a
  short hint.
- [Mac mini] **Side panels:** Layers is 240 px by default, 220–400 px by dragging its edge, remembered. Window ▸ Layers
  hides the left panel; Window ▸ Properties hides the whole right dock, Capture included.
- [Mac mini] **Row 3's selection text:** "Hero · 480 × 320" for one object (its name, else its kind), "3 objects", or
  "Nothing selected".
- [Mac mini] **Suggested, not done yet:** remove the floating task bar's own "Ask AI…" field (row 3 has Ask now) and
  the Frame presets list in Properties while the Frame tool is on (the island has "Drop a size…").
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
