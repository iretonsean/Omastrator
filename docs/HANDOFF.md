# Handoff (2026-09-29, evening)

For the next session after a context clear. **Start with "Next session starts
here" just below.** Everything under "Earlier notes" is history.

## Next session starts here

### State (2026-09-29 ~6 PM Pacific, the Intel machine)
- **Phase 4 is merged and pushed** (9c5a487). `main` holds Browser View
  phases 1–4, and all 147 tests passed on the merged code, Chromium included.
  - Fix round 3: BUGS.md items 2–6, the `clearPending` race, Custom… with
    opacity.
  - The stale picture was real: a frame paused and shown again stayed hidden
    in Chromium, so its picture stopped. It's fixed with focus emulation on
    resume (cb4bf05), with tests on the dev-server path.
  - The drawn-picture tests: 390 shows the narrow layout, and a stale picture
    is drawn at 1:1, clipped.
  - Fix round 4, from one review: a crash when a clear ran inside an undo,
    a kept edit's stale `before`, whole-px rounding keeping the fixed edge,
    and the snapping race that made `LiveFrameTests` flaky.
  - Fix round 3b fixed the tests that failed on this machine: the default
    font (Liberation Sans), a global `vercel` on PATH, and the hostname
    `omarchy` in `sweep.sh`.
- **Also merged today:**
  - Qt 6.4 builds again, with a CI job on Ubuntu 24.04's Qt 6.4, pinned by
    digest. Its first run passed.
  - One-pass `.arg()` wherever outside text comes before a number
    (`fix/arg-chains`).
  - `Setup+Cli.cpp` (`refactor/setup-cli`).
  - `check.sh` judges by ctest's exit code, because newer CMake always
    prints "0 tests failed".
- **Not merged:** `claude/sponsorship-links` waits until the author sets up
  GitHub Sponsors (there's no listing yet). `claude/variables-manager-backlog`
  is docs only and safe to merge.
- **Load-sensitive tests** (they pass alone and failed only under heavy
  load): `PdfHostileInputTests` (time limits), and before fix round 4,
  `BrowseToolTests` (its wait was made readiness-based).

### Next, in order
1. **The author tests Pages as Workspaces** on the real desktop
   (docs/WORKSPACES.md, "Left for the author"): Super+Tab and the swipe
   reach the named workspaces, the Lua move and focus dispatches work, and
   New Page doesn't flicker.
2. **One announcement video** for Browser View and Pages as Workspaces:
   extend `v3-kinetic`'s style (in the private repo,
   `promo/media/animation/browser-view-v3/`) with a Pages as Workspaces
   section, about 45 s in total. Re-record Browser View beats whose look
   changed (the selection boxes, "Save failed"). Record the desktop beats on
   a Hyprland headless output. Draft the X post; the author posts it.
3. **Motion in Browser View runs in the cloud,** from the private repo's
   `briefs/motion/MOTION-CLOUD-HANDOFF.md`. An Opus session writes
   `docs/MOTION.md` on `feat/motion` and stops for the author's approval.
   Then a new Sonnet session builds it phase by phase, one branch per phase
   (`feat/motion-a`, …), with reports in the private repo's
   `briefs/reports/`. A local session reviews and merges each phase.
4. Phase 5, as listed under "Next, in order" below.

### The Intel machine (moved from the Mac on 2026-09-29)
- x86_64, 12 threads, 15 GB, Qt 6.11, Chromium 152, CMake 4.4. No battery:
  the CPU is capped at 45 W, so more build jobs cost memory, not power.
- **Builds:** `-j5` through `omastrator-build-slot` (two slots), configured
  with `-DCMAKE_LINKER_TYPE=MOLD`. Never in /tmp or a scratchpad.
- **Worktrees:** `~/Projects/Omastrator-main` (main),
  `~/Projects/Omastrator-shortcuts` and `-share-device` (reusable slots
  with warm `build/`).
- **Omastrator is installed** in `~/.local`, from this merge, with setup
  applied.

### Earlier state (2026-09-29 ~11:15, the Mac, before the move)

#### Moving to another machine
- **Private material** lives in the private repo
  `github.com/iretonsean/omastrator-private`. It holds the briefs, the promo
  and animation work, a copy of Claude's memory, and bundles of the old local
  branches and the pre-rewrite backup. Its README has the setup steps.
- **The unmerged phase 4 code** is the `feat/live-in-frame` branch on GitHub.

#### State (2026-09-29 ~11:15, session ended for a reboot into macOS)
- **`main` is pushed** and CI is green (run 36593059112). It holds:
  - the Off-axis icon and the size fixes;
  - Browser View phase 2 (Pages as Workspaces, off by default);
  - phase 3 (the frame);
  - the no-Chromium test fix.
- **Phase 4, Live inside the frame, is NOT merged.** It's on `feat/live-in-frame`
  in ~/Projects/Omastrator-shortcuts, local only.
  - It was reviewed twice (A and B), fixed, verified, then got a second fix
    round (`lffix2`).
  - Fix round 2 was committed before the shutdown:
    - a frame no longer sticks on "Starting…" when it browses away mid-start;
    - the island is exactly as on main (frames never touch deployProject,
      m_lastProject, the island's write-back or live.deploy);
    - flaky-test fixes;
    - a frame's picture follows the page after a resize, and a stale one is
      never squeezed.
  - The branch may end with a `WIP:` commit. **Read the "Stopped for
    shutdown" line** in quick-wins/reports/feat-live-in-frame.md for what's
    unfinished.
- **Still to do on phase 4 before merge (the author, 2026-09-29):**
  1. **The picture stops refreshing once Live runs in a frame.** Edits and
     Build It reach the DOM, but the canvas keeps the old picture, even
     after Reload. The animation agent found it offscreen.
  2. **Real responsive reflow, not stretching.** During a drag or a held
     breakpoint, the reflowed page must reach the canvas, and a stale
     picture is drawn at 1:1, top-left, clipped, never scaled. The author
     was explicit that this is how the app must work.
  3. **Tests on the drawn picture:** at 390 it shows the narrow layout,
     with a Chromium test and a pure renderer test.
  - Then run check and sweep, and the lead reviews the diff and merges.
    **No more review agents** (see the pace note below).
- **The pace (the author, 2026-09-29): "can we speed this up".** One
  review, one fix round, then the lead merges. Run the next independent
  work in parallel.
- **The island:** the interim rule stays (decided 2026-09-29). Don't build
  on it. Phase 4's "island step" isn't built: ask the author first.

### The Browser View animation (ON HOLD: the author rejected the fast cut on 2026-09-29)
- Everything is in `~/Projects/.omastrator-promo/media/animation/browser-view/`.
  It never goes in the repo.
  - `BRIEF.md` holds the author's interview answers and later decisions.
  - `storyboard.html` is published at
    https://claude.ai/artifact/6jhUBaQ9i25mT9rz1knhVx.
  - `captures/` holds the real captures, and `capture.cpp` drives the real
    window offscreen.
- **The fast cut:** `omastrator-browser-view.mp4` was rendered before the
  shutdown from `browser-view.html` (30 fps).
  - Beat 3 shows no resize frames, because the captures showed the stretch
    bug.
- **Still to do:**
  - after phase 4's reflow fix merges, recapture beats 3, 4 and 7;
  - redo beat 1 on the real desktop, **using a Hyprland headless output**
    (`hyprctl output create headless`) rather than the physical screen,
    which is often on the Mac's input;
  - render the final cut at 60 fps;
  - draft `X-POST.md`.
- **The live check of Pages as Workspaces** (does Super+Tab reach the
  `design:` workspaces; do the Lua dispatches work) was attempted during
  the beat-1 capture. See the animation agent's result in the storyboard's
  notes. If it's not there, it still needs doing.
- **Localhost** stays in the address bar until a production domain can be
  tested.

### Next, in order
1. Finish and merge phase 4 (the three items above).
2. Recapture and render the final animation, and draft the X post.
3. Phase 5: pinning design objects to page elements so they follow the
   reflow, Duplicate at Breakpoints, and Clean Session. Then tick
   QUICK-WINS item 9.
4. Stop there. MEDIUM-EFFORT.md is on hold, and Effects waits on the
   author.
- A small follow-up: `.arg(x).arg(n)` in page naming mangles names that
  contain `%1` or `%2`.

### Waiting on the author
- **The history backup:** delete it when the author says so. Optionally, ask
  GitHub for a cached-views purge so the old commits stop resolving.
- **The alpha tester (@Madmasx):** the author replied, and the tester liked
  the reply. The setup and bar fixes are pushed. If the tester reports keys
  dying again, ask for their Hyprland version, whether their config is Lua
  or conf, and `hyprctl binds -j`. The cause wasn't reproduced; the leading
  theory is a latched submap (docs/OS-SUITE.md).
- **Installed build:** `~/.local` still runs the build from 2026-09-28
  10:09, before all of today's work. Reinstall with
  `scripts/install-local.sh` and restart the daemon only with the author's OK
  (it closes their window).
- **Their own steps:** publish to the AUR and cut the first release
  (docs/RELEASING.md); supply real Figma, .ai and PDF files to check the
  importers against.
- **Try on the desktop:** Pages, Lock Document, frame presets, per-side
  padding, settings export and import, the sticky bar, and the island rule.
- **Parked:** the frame-resize default for paths (don't raise it), and the
  Inspect sluggishness (at the Mac, with `scripts/profile-design-mode.sh`).

### Backlog written today (not scheduled)
- docs/backlog/GIT-NATIVE.md: readable files, a file CLI, an agent skill,
  and linked libraries. The ideas come from Elyx.
- docs/backlog/BACKGROUND-WORK.md: heavy work off the UI thread.
- docs/backlog/INTERACTIVE-BEHAVIORS.md: behaviours that break the fourth
  wall.
- QUICK-WINS.md "Backlog, not queued": refresh the README screenshots.
- Small follow-ups that are noted but not queued:
  - QSettings lives under "Unknown Organization" because the app never sets
    an organization name (fixing it needs a migration);
  - Share and Send to Device render PNG at a fixed 2x;
  - Setup.cpp is over 700 lines (`runCli` could move to `Setup+Cli.cpp`).

### How the work runs (keep doing this)
- **Agents, in Herdr panes:**
  - Sonnet 5.5 (`--model claude-sonnet-5-5 --effort high`) codes.
  - Opus 5.5 (`--effort medium`) reviews every branch before merge, and
    checks each fix round.
  - The `sonnet` alias is pinned to 5.5 in ~/.claude/settings.json.
- **Briefs, reports and reviews:** `~/Projects/.omastrator-briefs/`.
  - `quick-wins/SHARED.md` holds the rules every coding agent reads, and
    `REVIEW-ITEM.md` and `VERIFY-FIXES.md` are the review briefs.
  - Reports go in `quick-wins/reports/` and reviews in `reviews/`.
- **Builds:**
  - Always go through `omastrator-build-slot`, at `-j3`, with at most two
    builds at once.
  - `check.sh build 3` must print CHECK OK and `sweep.sh` must print SWEEP OK
    before a merge.
  - Never put a build folder under /tmp or the scratchpad: both are RAM.
- **Never:**
  - start a Hyprland (only `--verify-config`);
  - touch the running daemon or the user's desktop;
  - let tests reach the real HOME or XDG folders;
  - add promo material;
  - push without the author's OK.
- **Merging:** chain the steps with `&&` so a conflict stops everything that
  follows. One bad merge was committed with its conflict markers and had to
  be reset.
- **Worktrees in use:**
  - `~/Projects/Omastrator-main` holds `main`.
  - `~/Projects/Omastrator-shortcuts` and `-share-device` are reusable slots,
    each with a warm `build/`.
  - `~/Projects/Omastrator` (the main checkout) is on the old
    `feat/inspect-inside`; leave it alone.

## Earlier notes (history)

### Branch-by-branch notes from today (the detail)

**Update 2026-09-28: queued item 6 (resize artboards on the canvas) is built**
on `feat/artboard-resize` (off `feat/inspect-inside`). Not installed; the
daemon still runs the earlier build.

The decision, Figma's: an artboard behaves like a top-level frame.

- **Moving** carries its art with it, whatever the art's constraints.
- **Resizing** applies each object's constraints, as a frame's children do
  (`VectorDocument::constrainToBox`, the loop `resizeFrame` now shares). The
  default is Left and Top, so art rides the top-left corner and stays put when
  a right or bottom handle is dragged. The Layout section's constraints change
  that per object.
- "The art" is what the Artboard tool already used: the layer children whose
  centre sits on the artboard (`artCenteredIn`). The Artboard tool's "Move art
  with artboard" option still turns all of it off.
- This applies to W and H in the Properties panel as well, so the panel, the
  handles and the Artboard tool all resize the same way.

What's built:

- **Selecting:** with the Select tool, a click on an artboard's name above it
  selects the artboard (`EditorSession::selectArtboard`, `artboardSelected`).
  Picking any object, or an empty click, ends it. Names now show on the Select
  tool even with one artboard. If a frame's name sits at the same corner, the
  artboard's name stacks above it.
- **Handles:** a selected artboard shows the object handles (no rotate zone).
  They snap and use smart guides like an object's (the artboard itself and the
  art riding with it aren't targets), Shift keeps the ratio (side handles
  too), Alt works from the centre. `handleScale` is now shared by objects and
  artboards.
- **Moving:** drag the name; Alt-drag duplicates, and the copy is selected.
  Snaps to other art and artboards.
- **Delete** removes the selected artboard (never the last one).
- One undo step per drag ("Move Artboard", "Resize Artboard"); Escape cancels.
- **Tests:** tests/Document/ArtboardResizeTests.cpp, tests/Canvas/ArtboardSelectTests.cpp
  (ArtboardToolTests still pass unchanged).
- **Verified:** the tests, and an offscreen grab of a selected artboard with a
  frame at its corner (handles and stacked names look right).

Needs the author: try it by eye on the desktop (the look of the names with
one artboard, and whether the stacked name is easy to hit).

Not done: nudging a selected artboard with the arrow keys, X and Y fields for
it in the Properties panel, and artboards in the agent tools.
**Update 2026-09-28: queued item 5 (keyboard shortcuts) is built** on
`feat/shortcuts` (off `feat/frames`, 95/95). The full comparison with Figma's
keys, every conflict and the choice made are in docs/SHORTCUTS.md.

- **Why Shift+A failed:** it was a canvas key, so it only fired while the canvas
  held focus. After a click in Layers or on any panel button it did nothing,
  and the Object menu showed no key for it. Now Shift+A, Alt+Shift+A, Shift+H
  and Shift+V are menu entries (they work from any focus, show in the menu, the
  task bar tooltips and Ctrl+K, and can be remapped; text fields keep their
  capitals). Tool letters, X, D, digits (opacity) and, from a plain button,
  arrows and Delete now reach the canvas from a panel too
  (`ContentView::panelKey`); text fields, spin boxes, combo boxes and lists
  keep their keys.
- **New keys:** Figma's K, O and Shift+P (second names for Scale, Ellipse,
  Pencil); Enter (into groups and frames, or edit selected type), Shift+Enter
  (to the parent), Tab and Shift+Tab (next and previous sibling, wrapping),
  Esc (deselect, after leaving isolation and clearing picked anchors);
  Ctrl+Shift+L, Ctrl+Shift+H, Ctrl+Alt+M, Shift+0 and Alt+1 as second keys;
  Ctrl+Shift+E for Export for Screens; Alt+8 for Properties.
- **Discoverable:** Keyboard Shortcuts moved to Help with Figma's Ctrl+Shift+?;
  the sheet lists the fixed keys; tool tooltips, the palette and menus already
  showed keys, and the Properties panel's auto layout, Detach and swap/reset
  tooltips now read the current key (`ShortcutSettings::tip`).
- **Left unbound on purpose:** Alt+A/D/W/S/H/V (Figma's align keys) clash with
  the menu bar's Alt mnemonics; Figma's Ctrl+D, R, L, C, S, A, N and Ctrl+0
  lose to Illustrator's keys (docs/SHORTCUTS.md).
- **Verified:** tests for the menu keys, the panel routing (a button and a text
  field with focus), Enter/Tab/Esc on the canvas, and the definition counts.
  Not looked at on the real desktop.
- **Needs the author:** try Shift+A after clicking in Layers; say whether Esc
  deselecting and Tab walking the selection feel right (Tab still moves focus
  when nothing is selected); say whether the Figma column in docs/SHORTCUTS.md
  matches what they know (it came from cheat sheets, not a running Figma).
  Saved remaps of "Add auto layout" from the last day are dropped (the key
  moved to the Menus group); nothing else is affected.
**Update 2026-09-28: queued item 3 (Share ▸ Send to a device) is built** on
`feat/share-device` (off `feat/frames`). It's in File ▸ Send to a Device…,
Ctrl+K ("AirDrop") and the Share popover. The full description is in
docs/SHARE.md ("Send to a device").

- **What it does:** renders the selection (else the artboard) as PNG at 2×,
  PDF or SVG to a temporary file, lists nearby Apple devices with
  `omdrop peers --json` (turning AirDrop on for 5 minutes with `omdrop on 5m`
  if it's off), lets the author pick one (the last one used is chosen first,
  in QSettings), and sends with `omadrop send --quiet --to <address> …` (omdrop
  alone if omadrop isn't installed). Progress and the result use the Share
  toast, with Cancel. Missing omdrop, no devices, declined, a phone that
  didn't wake and other failures each have their own plain line.
- **Verified:** `DeviceShareTests` (14 cases) against a fake omdrop and omadrop
  (`OMASTRATOR_OMDROP`, `OMASTRATOR_OMADROP`), so no test touched the radio;
  the popover and toast were grabbed headlessly and look right.
- **Not verified, needs the author:** a real send. Nothing was sent to a
  device, and `omdrop` was never turned on. Things to check with the phone:
  1. The device list and names (`peers -n --json --stream` takes 15 to 20 s;
     the names fill in as they arrive).
  2. That an iPhone accepts a PNG, a PDF and an SVG the same way.
  3. That `omdrop on 5m` from the popover is what they want: it makes the Mac
     visible to nearby devices for five minutes (omadrop's own send does the
     same).
- It doesn't touch omadrop's remembered device (its `device` file);
  Omastrator keeps its own last device.
**Dense panels (2026-09-28, feat/dense-panels):** the Properties panel is now
the A + C hybrid the author picked (docs/PANELS.md; the mock-ups are at
https://claude.ai/artifact/Jm2gU67JnMHBcTmDJKVAZk).
- Fields are one 24 px box with their label inside, and fields go in pairs.
- Layout's flow and remove sit in its heading.
- Blend and opacity share one row.
- Stroke takes 4 rows, down from 8.
- Align and Pathfinder start folded, and every folded section shows a
  one-line summary.
- For a frame with auto layout the panel is about 680 px, down from about
  1,020. It was checked headlessly (a grab of the panel), but the author
  hasn't seen it on the desktop yet.

**Update 2026-09-28 ~08:45: queued item 1 (Inspect inside windows) is built**
on `feat/inspect-inside` (off `feat/frames`, pushed, 95/95). It's installed in
~/.local, but it isn't live until the author does two things (ask them; don't
restart their browser or the daemon on your own):

1. Restart Chromium, or reload the Omastrator extension at
   chrome://extensions. The extension has new `scripting` and `<all_urls>`
   permissions and a new worker file (background-2.js).
2. Restart the daemon (the steps are below).

What it does:

- **Chromium pages (any tab, PWAs too):** when the pointer rests, the
  extension runs Inspect's page script in the tab the window shows, matched
  by title (`Omastrator.inspect` in background-2.js). No debugging bar shows.
  The script now lives once, in `extras/chromium-extension/inspect-page.js`,
  and is compiled in too (the `InspectPageScript.h` header generated by
  CMake). Verified in a headless Chromium with the extension loaded: it
  returned `button#buy` with its CSS.
- **Qt apps:** design mode turns on the a11y bus's `IsEnabled` while it runs
  (via busctl) and turns it back off after (`SystemSource::wantAccessibility`).
  Qt apps registered right after the flip, but nobody has yet seen a real Qt
  window's tree through Inspect: check this with the author.
- **Omastrator's own windows:** read in-process through QAccessible, because
  the daemon's blocked thread can't answer AT-SPI.
- **Not covered:** Electron apps (only AT-SPI if started with it) and
  terminals.
- **Watch this with the sluggishness:** with the a11y flag on, quickshell
  publishes its QML tree too, which could add load while design mode runs.
  Compare with and without it when profiling.

**State at 2026-09-28 ~08:20.** Everything is on `feat/frames` (pushed; 95/95
suites pass). It's stacked on `feat/graphite-look`, which is stacked on
`fix/design-mode-escape`. None of them is merged into main yet.

- **Installed:** the app, from `feat/frames` at 1825fff, in ~/.local. The shell
  plugins are synced too (the author ran `omarchy restart shell` at 08:01).
- **The daemon** (the app window the author works in) was restarted on the
  08:15 build at the author's OK (08:30), so Name with AI is live.
  - For later restarts: find it with `pgrep -x omastrator -a`, kill that PID,
    and start it with `systemd-run --user … omastrator --daemon`. Ask first
    when the author is at the Mac.
  - Never `pkill -f` a pattern that's in your own command line: it kills your
    own shell.
- **Done this morning:**
  - The floating bar's Ask took no keys: typing P picked the Pen underneath.
    The overlay now claims the keyboard on the click (`window.asking` in
    Overlay.qml).
  - Three older QML errors in Island.qml and Overlay.qml are fixed.
  - **Name with AI** (1825fff):
    - The agent's standing instructions (`AgentLauncher::instructions`) require
      real layer names.
    - A `rename` tool renames many objects in one call.
    - The Layers footer has a "Name with AI" button. Its arrow offers naming
      conventions, and the last one used is kept in QSettings
      `layerNamingConvention`.
    - `AgentLauncher::namePrompt` and `AgentBridge::nameLayers` drive it.
- **Open investigation: Inspect makes the desktop sluggish** (the author,
  08:05). Nothing is fixed yet. **Parked until the author says they're ready**
  (08:25): it needs them at the Mac to reproduce, so don't start it on your
  own. When they're ready, run the profiler with them.
  - What's measured so far: each Inspect step is cheap on its own, under
    0.1 s. That covers the AT-SPI helper, grim for 1 px, and hyprctl. The
    status payload is 1.3 KB. When idle, the daemon uses 0.3% CPU.
  - DesignMode polls every 100 ms on the daemon's main thread. It spawns
    `python3` (AT-SPI) and `grim` when the pointer rests, and the web lookup
    waits in a local event loop.
  - The suspects to check next:
    1. Hyprland redrawing the full-screen Overlay layer on every hover change
       (a repaint of a whole-screen layer surface);
    2. quickshell re-evaluating bindings on each status line;
    3. the page lookup in a browser blocking the daemon.
  - How to catch it: run `scripts/profile-design-mode.sh`, which waits for
    design mode and then records 90 s of per-process CPU. Have the author use
    Inspect over a few windows while it records.
  - Two recordings this morning caught nothing, because design mode was never
    turned on while they ran.
  - Ask the author whether it's sluggish everywhere or over certain apps.

Next, in the author's order:

1. The queued feedback below.
   - Some items are waiting on the author: the frame resize default, and
     picking a panel design from the options.
   - Others can go ahead without them: Inspect reaching inside windows (turn
     on AT-SPI), Share to a device (built, see above), the shortcuts pass, and resizing artboards
     on the canvas.
2. docs/FIGMA-AUDIT.md's build order, with effects next.
3. The Inspect sluggishness, when the author is at the Mac and says they're
   ready.

**Merging:** fix/design-mode-escape, then feat/graphite-look, then
feat/frames, each with `scripts/merge-branch.sh`. Merge when convenient,
after the author has seen the look.

### The author's feedback on the desktop (2026-09-28), queued and not started

1. **(Built on feat/inspect-inside; see the update at the top.) Inspect should reach inside windows.** It should read the content in a
   window (its controls and text, down the AT-SPI tree or the DOM), not only
   the window's bounds and Omarchy's own elements.
   - Found on 2026-09-28: the AT-SPI helper (`pythonHelper` in
     src/Anywhere/Inspect.cpp) answers "This app has no accessibility tree"
     for Chromium and "Nothing accessible is there" for Omastrator's own Qt
     window. So most windows fall back to their bounds.
   - Likely needs: Chromium's accessibility switched on
     (`--force-renderer-accessibility`, or the extension or DevTools route
     that Live already has); Qt apps run with the AT-SPI bridge on
     (`QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1`); and GTK apps checked.
   - Web pages already go through the DOM when Omastrator's browser link
     reaches the tab.
2. **Parked indefinitely (the author, 2026-09-28): don't start it or ask about it.**
   **A path in a frame should resize with the frame, keeping its
   proportions.** Today paths default to the Left/Top constraints and stay put.
   The proposal waiting on the author's answer:
   - vector paths default to Scale with their aspect ratio kept;
   - text and images keep Left/Top, as Figma users expect.

   The alternative is that everything scales. Constraints live in
   `LayoutItem`; the resize is `VectorDocument::resizeFrame` in
   src/Document/AutoLayout.cpp.
3. **(Built on feat/share-device; see the update at the top.) Share ▸ Send to a device.** Send the selection to the author's iPhone
   through the AirDrop plugin installed in Omarchy (see the omadrop project,
   `omdrop-awdl`).
4. **The panels are bloated.** There's too much vertical scrolling. Keep every
   feature, but design a new UX that frees up vertical space without hiding
   everything behind a ••• button. The layouts inside the sections need design
   work too. Do a design pass first and show the author options. Ideas already
   mentioned to them:
   - sections that show or collapse by what's selected;
   - fields side by side in pairs;
   - tabs.

   Don't rebuild anything until they pick an option.
5. **(Built on feat/shortcuts; see the update at the top.) Keyboard shortcuts feel missing.** Check the keys against Figma's.
   - Shift+A is there, as a canvas key, and needs a selection and canvas
     focus. Find out why the author didn't find it working, or didn't find it
     at all.
   - Then close the gaps and make the keys easy to discover: in menus,
     tooltips and the Ctrl+K palette.
   - The keys added this session:
     - F: the Frame tool;
     - Ctrl+Alt+G: Frame Selection;
     - Shift+A: add auto layout;
     - Alt+Shift+A: remove auto layout;
     - Ctrl+Shift+G: Remove Frame.

     The Object menu lists Add Auto Layout without its key, because the key is
     a canvas key so that a capital A still types.
6. **(Built on feat/artboard-resize; see the update at the top.) Resize an artboard on the canvas as you would a frame,** with the Select
   tool, not only with the Artboard tool.

The earlier notes follow.

1. Chromium was started with the extension, and the whole path worked on the
   real machine (2026-09-27 22:19): the extension linked,
   `live start --tab --folder ~/Projects/seanireton.com` joined the
   localhost:5173 tab (not a mock-up), a page screenshot came back through
   chrome.debugger, and Stop released the tab. The screen was asleep, so
   nobody has looked at the side panel UI or the debugging bar yet: ask the
   author to open the panel (the toolbar button) and try Start, Cancel on the
   bar, and Stop. The site's `npm run dev` was left running (Vite, :5173).
   Ask how "Tidy it up" felt on a drawing too.
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

## Frames (branch feat/frames, off feat/graphite-look)

Queue item 3 has started. docs/FIGMA-AUDIT.md compares Figma's design core
with what's here and sets the build order. The first item, frames, is built:

- **The model.** `ObjectKind::frame` is a container with a box of its own:
  - `shape`, a `LiveRectangle` that's always live, with `path` kept to it;
  - fills and strokes;
  - `clipsContent`, on by default.

  The box is painted under the children and clipped to; the strokes go over
  them. Frames have their own bounds and hit tests, and what a frame clips
  away can't be clicked.
- **The Figma click rule:** a top-level frame lets a click through to its own
  child. A nested frame is picked whole, as a group is.
  `VectorDocument::selectableObject` holds the rule.
- **Selection:** a selected frame answers for its own paint (in
  `selectedLeaves`), so Fill changes the frame, not its children.
- **Tools and commands:**
  - the Frame tool (F, in the rail with the artboard);
  - Object ▸ Frame Selection (Ctrl+Alt+G);
  - Clip Content;
  - Ungroup, which removes a frame;
  - frame names above top-level frames on the canvas (a click on one selects
    the frame);
  - the `#` icon in Layers;
  - SVG export as a clipped `<g>`;
  - the codec (the `frame` kind; the box is required).
- **Tests:** tests/Document/FramesTests.cpp.
- **Resizing** follows Figma: a box resize on an upright frame changes only
  the box, and the children move and size by their constraints. A box resize
  is the handles, or W and H (the `reflowAreaText` flag).
  - The constraints are Left, Right, Left & Right, Center and Scale; the
    vertical ones are the same.
  - They're `LayoutItem::horizontal` and `vertical`, set in the Layout
    section, via `VectorDocument::resizeFrame`.
  - The Scale tool and Transform ▸ Scale still scale everything.

Other things still to do for frames:

- moving objects into or out of a frame by dragging;
- a Clip content checkbox in the Properties panel;
- frames in the agent tools and the lift;
- a frame preset list (phone, desktop), as Figma's Frame tool has.

**Auto layout** is built too, on the same branch (docs/AUTO-LAYOUT.md):

- **The model:** `AutoLayout` on a frame and `LayoutItem` on any object:
  - direction;
  - gap, or Auto (space between);
  - per-side padding;
  - primary and counter alignment;
  - wrap with a counter gap;
  - Fixed/Hug/Fill sizing;
  - Absolute position.
- **The engine:** `VectorDocument::applyAutoLayout`
  (src/Document/AutoLayout.cpp). It runs innermost first and repeats until
  nothing moves. It runs on every notify and prune and after a file is read.
  - A child frame resizes its box, a live rectangle its rect, and area type
    its width. Anything else stretches.
  - Rotated frames are skipped.
- **Shift+A:** gives a frame auto layout, reading the direction, gap and padding
  from where its children sit and putting them in that order. With loose
  objects selected, it wraps them in a new hugging frame with no fill.
- **Alt+Shift+A** removes auto layout, as does Object ▸ Remove Auto Layout.
- **The Properties panel's Layout section:**
  - Add;
  - Horizontal, Vertical or Wrap;
  - Gap and Auto;
  - padding in pairs;
  - the 3 × 3 alignment grid;
  - W and H sizing;
  - Absolute position;
  - Clip content.
- **Tests:** AutoLayoutTests and PropertiesPanelTests::theLayoutSectionDrivesAutoLayout.

Not done yet:

- dragging to reorder inside an auto-layout frame;
- editing padding per side (the panel sets the pairs);
- baseline alignment;
- min and max sizes;
- a canvas overlay for the gap and padding (Figma's pink handles).

Constraints are built (see Frames above). Next in the build order: effects
(drop shadow, inner shadow, layer blur, background blur), then live boolean
groups.

## The Graphite look (branch feat/graphite-look, off fix/design-mode-escape)

Queue item 2 was done while the author was away from the screen (2026-09-27, about
22:45). It was checked only in offscreen renders; nobody has seen it on the desktop
yet.

- **The island, floating bar and cards** read `O.Theme`
  (shell/omastrator-ui/Theme.qml), which uses Graphite's `[graphite]` shell tokens
  and falls back to the popup colours on other themes. `O.Panel` is a floating
  surface with a shadow, a top highlight and no border. `O.Lift` gives the lift
  under the pointer and the soft-accent fill for what's chosen. Labels are
  SF Pro Text 13; values (the Inspect card, measurements) are SF Mono.
- **The app** reads the same component tokens from colors.toml
  (`OmarchyColors::components`). `OmarchyStyle` (src/UI/OmarchyStyle.cpp, Fusion
  underneath) draws:
  - no outlines;
  - rounded fills;
  - the lift on hover;
  - the soft accent for default buttons and chosen list rows;
  - thumb-only scroll bars;
  - a 4 px slider;
  - framed lists as rounded groups.

  The pasteboard uses `QPalette::Dark`. Themes without tokens keep their palette
  and get the same shapes.
- **To see the app without a screen:** run
  `XDG_RUNTIME_DIR=<temp dir> QT_QPA_PLATFORM=offscreen OMASTRATOR_SNAPSHOT=out.png build/omastrator [file]`.
  It saves a 1440×900 picture and quits.
- **Installed:** the app (the release build is in ~/.local, and the daemon was
  restarted on it). **The shell plugins are NOT synced yet:** the session was
  locked, the lock screen belongs to omarchy-shell, and restarting the shell
  under a lock could leave a dead lock screen. Once unlocked, run
  `scripts/install-local.sh --shell`.
- **Ask the author:** does it look right? The riskiest parts are:
  - the island's panel tone over dark wallpaper (Graphite panels have no border;
    the shadow and top highlight separate them);
  - chosen tools in the tool strip, which lift rather than turn blue, since the
    accent would drown their icons.

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
     every submap; it exists only after `omastrator setup --apply`). They stop the agent's overlay work, drop proposals,
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
  installed build: installs, then runs on localhost:5173 (ae89886).
- **The island ticker:** a message's first line goes in the pill, and the rest
  (or the whole line, when the pill cuts it) goes in a wrapped card under it.
- **Live in your own Chromium** (3fe7785, docs/OS-SUITE.md "Live in your own
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
