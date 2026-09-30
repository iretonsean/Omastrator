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
