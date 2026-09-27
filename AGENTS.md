# Omastrator: notes for agents

Omastrator is a vector illustration app for Linux, modelled on Adobe Illustrator
and made first for Omarchy. It is C++20 with Qt 6 Widgets (Qt 6.4 at minimum),
built with CMake and tested with Qt Test.

## Layout

Each folder builds as its own static library:

- `src/Document`, `src/Rendering` → `oma_core`. The model (`VectorDocument`,
  `VectorPath`, `Paint`), `EditorSession` (every edit, selection, history and
  view state; it emits `changed()` and `documentChanged()`), `DocumentHistory`,
  `DocumentCodec` (the JSON shared by `.omai` files and the clipboard),
  `PathOperations` (shapes, the boolean operations, offset, simplify),
  `ImageTrace`, and `VectorRenderer`, which the canvas and every export draw
  through.
- `src/IO` → `oma_io`. `ProjectStore` (`.omai`), `SvgImporter` (vendored
  nanosvg in `third_party/`), `SvgExporter`, `DocumentExporter` (PDF, PNG,
  JPEG) and `ImageImporter`. Errors are thrown as `FileError`.
- `src/Canvas` → `oma_canvas`. `EditorCanvas` and its tools, `SmartGuides` and
  `InlineTextEditor`.
- `src/UI`, `src/ContentView*` → `oma_ui`. The window, tabs, panels, menus,
  sheets, shortcuts and the Omarchy theme.
- `src/OmastratorApp.cpp` holds `main`.
- Tests live in `tests/<Folder>/*Tests.cpp`, one executable per file, found by a
  glob.

## Rules

- **Parallel builds:** keep them to `-j3` or fewer. A `-j10` build ran this
  15 GB machine out of memory.
- **Edits:** every document edit goes through `EditorSession` so it becomes one
  named undo step. Drags use `beginInteraction`, then a `preview*` call, then
  `commitInteraction` or `cancelInteraction`.
- **Style:** comments are one line and say why. Use Qt types directly and add no
  wrapper types. A file over about 500 lines splits as `Name+Part.cpp`.
- **Humor:** follow `docs/HUMOR.md`. Menu items, buttons, data-loss prompts and
  accessibility text are never jokes.
- **AI features:** follow `docs/AI-ROADMAP.md`.
- **Commits:** public repo. Commit as the GitHub no-reply address, and never add
  personal data.

## Provenance

- **OmaPhoto** (ZacharyZhang-NY/OmaPhoto, MIT, itself a port of Wonder
  Assembly's Compositor): the theme, shortcuts, floating panels, viewport,
  history, workspace and tabs, layer list, colour picker, sheets, blend modes,
  canvas navigation, inline text editing and the packaging.
- **omadesign** (michaelmonetized/omadesign, MIT): `ImageTrace` and
  `SmartGuides`, ported from Rust.
- **nanosvg** (memononen/nanosvg, zlib): SVG parsing, with one patch marked
  "OmaIllustrator patch".
