# Handoff (2026-09-27, late evening)

For the next session after a context clear. **Start with "Next session starts
here" just below.**

## Next session starts here

1. Ask the author whether they restarted Chromium and tried the Omastrator
   extension's panel. Ask how "Tidy it up" felt on a drawing too.
2. If anything in the extension is broken, fix it first: it's the way in for
   the lift (item 4).
3. Otherwise, go on with the queue below: **item 2, the Graphite theme**, then
   **item 3, Figma's design core** (frames, then auto layout, …), then the
   editable lift.
4. Merge `fix/design-mode-escape` into main when it's convenient; the steps
   are under "Where things are". Everything on it passes, 93 of 93.

Product rule from tonight: Omastrator is a **Figma/Illustrator hybrid built
into Omarchy** (docs/VISION.md). Never call a Figma design feature out of
scope. Import interfaces as editable layers, never as a traced screenshot
(tracing is for small graphics only).

The first handoff follows. The author tested the installed
build on their desktop all evening and reported problems as they found them.
Everything below is either done and installed, or queued in the author's
order.

## Where things are

- **Branch `fix/design-mode-escape`**, off `main`, not merged yet. It holds
  every fix from tonight (commits listed below) plus `docs/SESSIONS.md` and
  this file.
  - Before merging, run `scripts/check.sh build 2` and `scripts/sweep.sh main`.
  - Then `scripts/merge-branch.sh fix/design-mode-escape` from `main`. It
    runs check and sweep again and pushes.
- **Installed on the author's desktop:** everything on this branch, including
  the AI waiting line and frozen surface, installed with
  `scripts/install-local.sh --shell` and followed by a daemon restart. The
  author hasn't yet retried "Tidy it up" on a drawing with it; ask how it
  felt.
- **Branch `wip/promo-v3`** is the promo's third cut and the new app icon.
  Nothing there is merged.
  - The animation is `media/promo/promo-v3.html`. The review page is
    https://claude.ai/artifact/3y4aJA9zbKdYfjpGHAPG37 (a copy with the
    doctype, html, head and body tags stripped, plus the asset files).
  - The author hasn't given notes on the animation yet. It runs about 105 s,
    and cuts were offered: the capture beat, the roast, the Desk's second
    line, and a shorter thesis.
  - The new mark is "Off-axis": on an ink tile, a saffron pill top-left, a
    paper pen stroke through the exact centre, and a cobalt open-square point
    bottom-right. It's in `packaging/icons/app-icon.svg` and the PNGs (commit
    9b61b76), and CMake installs the SVG as the scalable icon.
  - The palette is flat: ink #14151D, paper #F1ECE2, saffron #F2A93B, cobalt
    #3D5AFE.
  - Render only after the author approves:
    `node render.mjs --page promo-v3.html --out omastrator-promo-v3.mp4 --workers 3`

## Done tonight (fix/design-mode-escape)

1. **The island stays reachable, Esc always leaves, clear all.**
   - With a drawing tool, the overlay leaves the island's pill out of its
     input. It places the pill using Hyprland's `reserved` top, published as
     `monitors[].reservedTop`.
   - `DesignMode::setOn` holds or releases the `omastrator-design` submap
     however design mode was entered, but only if `Setup::submapDefined` says
     Hyprland defines it.
   - `design clear all` and the island's Clear drawings button remove all
     overlay art.
   - The agent socket now accepts `lift`, `look`, `restyle` and `reset`;
     `lift`, `look` and `restyle` always failed before.
2. **The floating bar.** It never goes over the bar or island (`topClear`),
   waits at the bottom for screen-sized things like the desktop, folds down
   to its title line, and stays wherever it's dragged until Unpin. That state
   is kept in `PersistentProperties`.
3. **Escape hatches**, from a two-agent audit:
   - `omastrator reset`, `design reset` and Super+Alt+Escape (bound outside
     every submap). They stop the agent's overlay work, drop proposals,
     lifts and previews, clear the drawings (undoable) and give the keyboard
     back.
   - Every island mode change goes through `Island::holdKeysFor(mode)`, so no
     mode keeps its letters bound. Before this, Live's `d` could deploy after
     Live was left by a click.
   - Dictation restores the mode's keys when it finishes.
   - Desktop Look previews are discarded when design mode ends, when the
     panel closes, when the app quits, and on the next start after a crash
     (`look-pending.json` in the runtime directory).
   - A proposal left without the bar gets its own Keep/Discard card, and a
     folded bar still shows Keep, Discard and Cancel.
   - The text box closes with its tool, and with a drawing tool the overlay
     takes Esc itself.
   - Status followers exit with their parent (`PR_SET_PDEATHSIG`). About 40
     orphans had piled up.
   - The shell clears the state of a dead stream.
4. **Design mode starts on Point:** click-through, nothing inspected, no bar.
   Inspect moved right in the island's row. `DesignMode::setTool` rereads
   what's under a resting pointer.
5. **The AI waiting line and a frozen surface:**
   - While the agent works on overlay art, the surface shows its art as it
     was, with a pulsing outline, instead of the half-made edit. The
     author's drawing had vanished for a minute.
   - The waiting line says what the agent is doing, from its calls
     (`AgentBridge::stepFor`: looking at it, N changes so far, checking how
     it looks, finishing up), with the seconds counting on the bar. There's
     a Stop button.
   - Overlay asks run Claude with `--model sonnet`. `$OMASTRATOR_QUICK_MODEL`
     overrides it, and `default` uses the agent's own model.
   - Not done: an instant local "Smooth" for pen strokes, using
     `PathOperations` simplify, as a fast alternative to asking the AI to
     tidy.

The rule behind all of this is in the author's memory notes and in
`docs/SESSIONS.md`: Omastrator never takes over the computer without an
obvious, always-working way out.

## Done after the handoff (same branch)

- **The seanireton.com folder bug:** the clone had no `node_modules`, so
  `npm run dev` died ("vite: command not found"), and the failure flashed for
  4 s as one cut-off line in the island. The dev server now installs packages
  first, and Live's steps and failures stay readable. Verified on the
  installed build: installs, then runs on localhost:5173 (fb8c828).
- **The island ticker:** a message's first line goes in the pill, and the rest
  (or the whole line, when the pill cuts it) goes in a wrapped card under it.
- **Live in your own Chromium** (eb280e6, docs/OS-SUITE.md "Live in your own
  browser"): an extension plus the `omastrator browser-host` native host plus
  `BrowserLink`. The side panel is the start panel docked to the window.
  Setup is applied on the author's machine: the flags line and the host
  manifest. The daemon was restarted, and the real host answered `folders` and
  status.
  - **Not yet seen on a lit screen:** Chromium must be restarted to load the
    extension. Then its toolbar button opens the panel. Check Start on a
    seanireton.com localhost tab, the debugging bar's Cancel, and Stop.

## Queue, in the author's order

1. **Live in the author's own Chromium**: DONE (see above). All that's left
   is the author's own check in Chromium. Original notes:
   - Live mode should work in any Chromium window they open, not only in a
     URL typed into Omastrator's window.
   - Omastrator only reads a page's DOM in its own browser
     (`surface.otherBrowser`), so this needs a way into the user's Chromium:
     an extension, or remote debugging.
   - The Live start panel's content is fine, but it should dock to the window
     Live is used on without getting in the way.
   - Starting their project `seanireton.com` from its code folder in that
     panel did nothing. Reproduce this first.
   - The island's status ticker truncates ("…") and looks sloppy. Give it
     room, wrap it, or move long lines to the bar.
2. **Match the installed Omarchy theme** (the author uses Graphite). The
   island, bar, cards and the app window should use the theme's fonts,
   colours and radii. The author called the current look ugly.
3. **Figma's design core** (the author, 2026-09-27: "a hybrid
   figma/illustrator app built directly into the omarchy os"). Omastrator has
   components with variants, artboards, multiple fills and strokes, and live
   rectangles. It has **no frame layer type**. First audit what's there
   against Figma's design features, then build in this order:
   - frames: nestable, with fill, corner radii, clip content and a size;
   - auto layout: direction, gap, per-side padding, alignment, wrap,
     hug/fill/fixed, Shift+A;
   - constraints and resizing;
   - effects: shadows and blurs;
   - boolean groups, per-layer export, and whatever else the audit finds.
   The editable lift (item 4) imports flexbox as auto-layout frames, so it
   lands after frames and auto layout exist.
4. **An editable lift from the author's own browser.** The author says
   tracing a screenshot of an interface is "horrible": it's fine for small
   graphics picked with the pointer, not for whole interfaces. Study Paper
   Snapshot first (installed in their Chromium, id
   lidfahaahiogmnlccifabccgplofocck). It injects a script, resolves computed
   styles, and puts `<x-paper-html>…</x-paper-html>` on the clipboard as
   text/html, which Paper parses into frames with auto layout, text, images
   and vectors. Build "Copy as layers" into our extension, and an importer.
   Original note: Capture or Lift on a
   site should give real text layers, groups and shapes on the Desk, like
   paper.design's Chrome extension, not a traced screenshot. This shares the
   way into the user's Chromium with item 1.
5. **Design sessions:** see `docs/SESSIONS.md` (agreed, not started).
6. **Minor audit leftovers.**
   - The tray light's tooltip says "Enter keeps it, Esc discards it" for app
     proposals. Check it isn't shown for overlay proposals.
   - Design mode without setup's keys, with the island on another monitor
     (partly covered by the overlay's own Esc).

## How to work here (learned tonight)

- **Don't kill the daemon with `pgrep -f 'omastrator --daemon'`.** The pattern
  matches your own shell and kills it. Use `omastrator daemon stop`, then
  `setsid -f ~/.local/bin/omastrator --daemon`.
- **After setup changes the keys, run `hyprctl reload`.** Hyprland doesn't
  reread Omastrator's sourced Lua file by itself.
- **Run `scripts/check.sh build 2` in the background.** In the foreground the
  tool's timeout sometimes killed it partway (exit 143). It takes about
  2 minutes.
- **Screenshots of the author's desktop:** `grim -s 0.5`, into the scratchpad.
- **When the author says they're stuck,** check `omastrator status` (design
  on, tool, proposal, overlays), then use `omastrator reset`,
  `design discard` or `design clear all`.
- **Keep `-j2`.** The machine has 15 GB and a RAM-backed /tmp.
