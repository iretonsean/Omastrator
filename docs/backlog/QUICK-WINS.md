# Quick wins (2026-09-28)

Small, contained features. Each is a day or less for one agent, and most
come from requests Figma or Illustrator users vote for that neither has
built (docs/research/unbuilt-features-2026-09.md). Build them after the five
finished branches are merged (see the top of docs/HANDOFF.md).

Every item follows AGENTS.md: edits go through `EditorSession` as one named
undo step, with tests, following docs/PANELS.md and docs/HUMOR.md.

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
