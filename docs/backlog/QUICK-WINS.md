# Quick wins (2026-09-28)

Small, contained features. Each is a day or less for one agent, and most
come from requests Figma or Illustrator users vote for that neither has
built (docs/research/unbuilt-features-2026-09.md). Build them after the five
finished branches are merged (see the top of docs/HANDOFF.md).

Every item follows AGENTS.md: edits go through `EditorSession` as one named
undo step, with tests, following docs/PANELS.md and docs/HUMOR.md.

## The queue (the next session works top to bottom)

Tick each item when it's done. Change `[ ]` to `[x]` and add the merge
commit and date, for example `[x] … (a1b2c3d, 2026-09-29)`, then commit this
file with the work. Don't delete finished items.

- [x] 1. Text stroke inside/outside (check first) (3a67d4f, 2026-09-28)
- [x] 2. Non-printing / non-exporting artboards (0cb7b82, 2026-09-28)
- [x] 3. Editable document presets (990731f, 2026-09-28)
- [x] 4. Frame presets (8957dc4, 2026-09-28)
- [x] 5. Per-side padding fields in auto layout (8ea4d54, 2026-09-28)
- [x] 6. Lock Document (read-only mode) (3399220, 2026-09-28)
- [x] 7. Settings that travel (1814038, 2026-09-28)
- [x] 8. Canvas size limit (check and document) (c4c35fb, 2026-09-28)
- [ ] 9. Browser View and canvas workspaces (high effort, high priority;
      see section 9). Phase 1, Pages: merged (91e8f6c, 2026-09-28).

**The queue ends here.**
- MEDIUM-EFFORT.md is on hold until the author says otherwise.
- HIGH-EFFORT.md isn't scheduled, apart from its item 1, which is item 9
  here.
- Effects waits on the author's review of docs/EFFECTS.md.
- GIT-NATIVE.md (readable files, a file CLI, an agent skill, linked
  libraries, flows) is in the backlog, not scheduled.

## Backlog, not queued

- **Interactive behaviours (fourth-wall design):** see
  docs/backlog/INTERACTIVE-BEHAVIORS.md (the author's idea, 2026-09-28).
- **Heavy work off the UI thread:** see docs/backlog/BACKGROUND-WORK.md
  (the author, 2026-09-28).
- **Update the screenshots on the public repo page** (the author,
  2026-09-28).
  - **Today:** the README's hero image and its 35-picture gallery
    (`docs/screenshots/`) date from 2026-09-26. That's before the
    Graphite look, the dense Properties panel, frames and auto layout,
    Inspect inside windows, the import formats and Pages. They were made
    by hand, and no script in the repo reproduces them.
  - **Do:**
    - add `scripts/screenshots.sh`, which renders each picture offscreen
      from fixed demo documents (`OMASTRATOR_SNAPSHOT`, the UI tests'
      `grab()`, and the QML plugins rendered offscreen, as before), so the
      next refresh is one command;
    - retake every picture;
    - add the new features (the Properties panel, auto layout, Inspect,
      Open from Figma/Sketch/PDF, Pages, the Browser View once it exists);
    - keep the made-up demo content and no personal data.
  - **Not promo:** these are product screenshots for the README, not the
    promo material the author keeps out of the repo.
  - **Done when:** the README shows the current app, and the script
    regenerates every picture.

## Agents and models (the author, 2026-09-28)

- **Coding** is done by **Sonnet agents at high effort**, one per item, each
  in its own worktree and branch off `main`. Start them in Herdr panes with
  `herdr agent start <name> --kind claude --pane <pane> -- --model claude-sonnet-5-5 --effort high`.
  Sonnet 5.5 was confirmed live on 2026-09-28. (Claude Code 2.1.283 prints
  an "unrecognized model" notice for it; that's harmless.)
- **Code review and thinking** (design questions, reviewing each branch
  before it merges, and anything that needs judgment) are done by **Opus 5.5
  agents at medium effort**:
  `--model claude-opus-5-5 --effort medium`.
- **Every item** gets an Opus review before it merges. The lead merges,
  ticks the item here, and asks the author before pushing.
- **Build rules:** `omastrator-build-slot` (in ~/.local/bin: two slots, the first being `~/.cache/omastrator-build.lock`) with `-j3`, so at most two builds run at once (the author, 2026-09-28). Never
  touch the running daemon.

## 1. Text stroke inside/outside (verify first)
- **Demand:** Illustrator, 213 votes and still unanswered. Adobe's text model
  blocks it. Affinity has it.
- **Today:** the renderer aligns strokes on closed paths
  (`src/Rendering/VectorRenderer.cpp:259`), and glyph outlines are closed, so
  it may already work on type.
- **Do:**
  - Check it with a render test on point and area type.
  - If it works, keep the test and add a README line.
  - If it doesn't, route text strokes through the aligned-stroke path.
- **Done when:** Inside and Outside on live, editable type render correctly
  on the canvas and in PNG, PDF and SVG exports.

## 2. Non-printing / non-exporting artboards
- **Demand:** Illustrator, 152 votes, no status.
- **Today:** `Artboard` has an id, name, rect and background
  (`src/Document/VectorDocument.h:263`).
- **Do:**
  - Add an `exported` flag (default true), stored as an additive codec key.
  - Add a toggle in Properties ▸ Document ▸ Artboards and the artboard
    context menu.
  - Make Export, Export for Screens, Share, and the PDF's page list skip
    flagged artboards.
  - Mark flagged artboards on their canvas label.
- **Done when:** a flagged artboard is visible and editable but absent from
  every export, and the tests prove it.

## 3. Editable document presets
- **Demand:** Illustrator, 150 votes, no status.
- **Today:** five presets are hard-coded in `src/UI/NewDocumentSheet.cpp:10`.
- **Do:**
  - Add Save Preset… (from the current size and units), Rename and Delete.
  - Store them in `~/.config/omastrator/presets.json`.
  - Keep the built-ins, which can be hidden but not deleted.
- **Done when:** a saved preset shows at the top of the list in the next
  session.

## 4. Frame presets
- **Demand:** it's a gap in docs/FIGMA-AUDIT.md: "Still to come: …
  presets".
- **Do:**
  - While the Frame tool (F) is active, Properties lists device and screen
    sizes: phone, tablet, desktop, social and paper.
  - Clicking one drops a frame of that size, as Figma does.
  - Reuse the presets file from item 3.
- **Done when:** picking "iPhone 16" makes a 393 × 852 frame.

## 5. Per-side padding fields in auto layout
- **Today:** per-side padding exists in the model. docs/FIGMA-AUDIT.md lists
  "per-side padding fields" as still to come.
- **Do:** add paired fields in the Layout section, with a toggle between
  uniform and per-side padding (PANELS.md pairs, 24 px).
- **Done when:** each side can be scrubbed in one undo step.

## 6. Lock Document (read-only mode)
- **Demand:** Figma, 44 votes plus duplicate threads (4/5).
- **Do:**
  - Add a per-file toggle in File ▸ Lock Document and Ctrl+K, saved in the
    `.omai`.
  - While locked, `EditorSession` refuses edits with a plain status line,
    the tab shows a lock, and selecting, inspecting, measuring, exporting
    and sharing still work.
- **Done when:** no tool, menu or agent call can change a locked document
  until it's unlocked.

## 7. Settings that travel
- **Demand:** Illustrator, 144 votes, "awaiting development".
- **Do:**
  - Add Preferences ▸ Export Settings… and Import Settings…: one JSON file
    with preferences, remapped keys, workspace and presets.
  - Offer "Keep on cloud storage" through the existing rclone code
    (`src/Cloud`).
- **Done when:** settings exported on one machine and imported on another
  give the same keys, workspace and presets.

## 8. Canvas size limit (verify and document)
- **Demand:** Illustrator's top idea (1,029 votes). Adobe shipped a 10×
  canvas in 2020, and an unlimited canvas is still "Under Review".
- **Today:** there's no obvious cap in the code, but that isn't proven.
- **Do:**
  - Test a very large artboard (for example 50 m) for drawing, zoom,
    precision and export.
  - Fix what breaks.
  - State the real limit in the README.
- **Done when:** the README states a tested limit.

## 9. Browser View and canvas workspaces
- **Why it's here:** it's high effort (HIGH-EFFORT.md, item 1), but it has
  a higher priority than anything in MEDIUM-EFFORT.md. The author placed it
  after the quick wins. It's the last item in the queue; MEDIUM-EFFORT.md is
  on hold.
- **Design:** docs/BROWSER-FRAMES.md, approved by the author on
  2026-09-28, including the headless Omastrator Chromium profile with a
  one-time sign-in.
- **Phases, each its own branch, Opus review and merge:**
  1. Pages;
  2. canvas workspaces (Hyprland named workspaces);
  3. the Browser View frame, the Browse tool, breakpoints as a preview, and
     streaming through the headless profile;
  4. Live inside the frame (the element bar, tokens, Review changes,
     History, Deploy, and "Build it" through the agent), then removing Live
     mode from the island;
  5. Duplicate at Breakpoints, the three pinning rules, and Clean Session.
- **Tick it off** only when all five phases are merged. Record each phase's
  merge commit next to the item.
