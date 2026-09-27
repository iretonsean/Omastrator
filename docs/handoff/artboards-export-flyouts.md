# Handoff: P2-1 artboards, P2-10 Export for Screens, P2-12 toolbar flyouts

Paused on 2026-09-27 because the usage budget ran short. Branch:
`worktree-agent-aedf73bd2a164881c`, merged with `origin/main` at 3f9a874.
Specs and acceptance tests are in docs/QOL-RESEARCH.md (P2-1, P2-10 and P2-12).

## Done

- Read the code these three items touch, and chose the design below.
- `src/Document/VectorDocument.h` declares `Artboard` (id, name, rect,
  background), `VectorDocument::artboards`, `VectorDocument::exportAssets`, and
  the artboard helpers. **Nothing is implemented yet**, so the branch doesn't
  link until `VectorDocument+Artboards.cpp` exists.

## Design (decided; keep it)

- **One source of truth for artboard 1's size and paper.** `artboards` empty
  means one implicit artboard at `(0,0,size)` named "Artboard 1". Once it isn't
  empty, `size` and `background` stay the truth for the first artboard's size
  and paper: `allArtboards()` returns the list with `[0].rect.setSize(size)` and
  `[0].background = background` applied. `setArtboards()` writes `[0]` back into
  `size`/`background`. Legacy code that writes `size` (the Desk, AgentTools
  crop, `blank()`) then can't leave stale state behind. The first artboard's
  *position* lives in `artboards[0].rect.topLeft()` and can move off the origin.
- **Exports go through `artboardDocument(i)`**, which translates the art so the
  artboard's corner is the origin, sets `size`/`background` from it, clears
  `artboards`, and drops layer children that don't overlap it (only when there
  is more than one artboard, so single-artboard SVG output stays byte-identical).
  Guides shift too.
- **Keep the exporters defensive:** at the top of `VectorRenderer::render`,
  `DocumentExporter` (render and writePdf) and `SvgExporter::serialize`, use
  `document.artboards.empty() ? document : document.artboardDocument(0)`.
  `Share::selectionDocument` and the AgentTools selection crop must clear
  `artboards` after cropping.
- `VectorRenderer::draw` fills the paper of every `allArtboards()` rect instead
  of `(0,0,size)`. Also fix `EditorCanvas+Paint.cpp` (shadow, paper, hairline
  and grid per artboard, plus name labels when there's more than one or the
  Artboard tool is active), `drawIsolated`, `SmartGuides` `m_boards` (every
  artboard), `EditorCanvas+Measure.cpp:21` and the align-to-artboard rect in
  `EditorSession+Objects.cpp:363` and `AgentEdits.cpp:168` (the active
  artboard), and `releaseGuides` (the active artboard's extent).
- **Viewport:** `CanvasViewport` centres on `document.size`. Resizing artboard 1
  moves that reference, so in `EditorSession::notify` compensate: when `size`
  changes, `viewport.pan += (new - old) * pointsPerPixel() / 2`. That keeps the
  document origin still on screen during Artboard-tool drags and undo. Set the
  remembered size in `loadDocument`.
- **Active artboard:** a session field (`m_activeArtboard`), not saved. It's set
  by the Artboard tool, the Artboards list, next/previous, and `select()` when
  the selection's centre lies on an artboard. `setArtboardSize` and
  `setArtboardBackground` apply to the active artboard. Fit Artboard in Window
  (`zoomToFit`) fits the active artboard: keep `viewport.fit(size)` when it's a
  single artboard at the origin, otherwise `zoomToRect`.
- **Codec v4** (additive, so it merges with the design-system agent's codec
  work): write `"artboards": [{id,name,x,y,width,height,background}]` only when
  the list isn't empty, and `"exportAssets": [ids]` only when there are some.
  Keep `width`/`height`/`background`. Decoding a v3 file gives the implicit
  artboard. Test that migration reports one artboard, "Artboard 1", at
  `(0,0,size)`.
- **EditorSession API** (new `EditorSession+Artboards.cpp`, each call one named
  undo step through `edit()`): `activeArtboard/setActiveArtboard`,
  `addArtboard(rect)` "New Artboard", `duplicateArtboard(i)` (placed to the
  right with a 20 pt gap, copying its art), `renameArtboard`, `deleteArtboard`
  (never the last one), `setArtboardRect` (drags use `beginInteraction` +
  `previewDocument` + commit, "Move Artboard" / "Resize Artboard", and art
  whose centre is on the artboard moves with it while
  `session.artboardMovesArt` is set), `fitArtboardToArtwork(i)` (the art
  overlapping it, or all visible art when none does, strokes included),
  `switchArtboardOrientation(i)`, `showArtboard(next/previous)` (activate and
  zoom), and `fitAllArtboards`. Export assets: `collectForExport()` and
  `removeFromExport(ids)`.
- **Artboard tool:** add `Tool::artboard` at the **end** of the enum
  (`toolInfo` in EditorSession.cpp is indexed by the enum) with raw
  "artboard", title "Artboard", key Shift+O (`{Tool::artboard, "o", 8}` in
  `KeyboardShortcuts.cpp` toolKeys, plus `Key_O` with Shift in the canvas's
  `toolForKey`). In `allTools`, place it after directSelect. New
  `EditorCanvas+Artboards.cpp`: a drag on empty canvas draws one, a drag inside
  moves it, eight handles resize it, a click activates it, Alt-drag duplicates,
  and Delete removes it. Add `DragKind::artboard`, the Dispatch cases, a cursor
  (handleCursor over handles), `ContentView::hint`, `ToolIcons` (a crop-mark #),
  and `ToolHeaders` (a plainBar with a "Move art with artboard" checkbox).
- **Menus:** Object ▸ Artboards (New Artboard, Duplicate, Rename…, Delete, Fit
  to Artwork Bounds, Switch Orientation, Next Artboard Shift+PgDn, Previous
  Artboard Shift+PgUp), View ▸ Fit All in Window, File ▸ Export ▸ Export for
  Screens… (Alt+Ctrl+E is taken by PNG; leave it without a key), and Object ▸
  Collect for Export. Every keyed entry needs a `ShortcutDefinition` entry, and
  `MenusTests::everyMenuKeyHasOneDefinition` counts keyed entries (69 now).
  Page keys aren't one-character chords, so that test only allows F7:
  either give next/previous no key, or extend the test. The canvas context menu
  under the Artboard tool (in `ContextMenus::forCanvas`, with a branch on
  `session.tool() == Tool::artboard`) shares those actions, and the empty-canvas
  menu gets an Artboards submenu. Rename uses a small `ObjectDialogs` sheet.
- **Properties ▸ Document:** an Artboards list (QListWidget, the active row
  selected, double-click renames, right-click gives the artboard menu, and a +
  button). W/H and Background edit the active artboard.
- **Share and Export** default to the active artboard: `ProjectWorkspace::exportTo`,
  `ExportSheet` (pass `artboardDocument(active)`), `ShareController`
  (`scopeText` gives "the artboard “Name”" when there are several), and
  `suggestedName` (the artboard's name when there are several).
- **Export for Screens (P2-10):** pure logic in a new `src/IO/ScreenExport.{h,cpp}`:
  `struct Settings { std::vector<double> scales; QStringList formats (png jpg svg pdf webp); QString folder; }`
  and `run(document, artboardIndices, assetIds, settings, progress)`, which
  returns the paths written. Files are named `<safe name><suffix>.<ext>`, where
  the suffix is empty at 1× and `@2x`, `@0.5x` and so on otherwise. Vector
  formats are written once, at 1×. WebP goes through QImageWriter "webp" and
  fails cleanly without the plugin. Assets are cropped the way
  `Share::selectionDocument` does it: move that function into core, or reuse it
  from IO. The UI is `src/UI/ExportForScreensSheet`: artboard and asset
  checkboxes, scale chips (0.5×, 1×, 2×, 3×, 4×), format checkboxes, a folder
  and Export. It remembers its settings in QSettings (`screenExport/*`), shows
  progress, and ends with "Open Folder" (QDesktopServices). A cloud destination
  is optional: it would reuse `CloudUploader::upload` per file as
  `exportAs` does.
- **Desk:** in `Desk::addFrame`, also add an artboard named after the frame's
  label at the frame's rect, via `setArtboards` on `next` before
  `previewDocument`. The first time, it materialises the Desk's own artboard as
  `[0]`. Replace the `next.size = …` growth line with growth of `size` alone
  (that stays correct, because `size` is artboard 1's size).
- **Flyouts (P2-12):** in `ContentView`, replace the button per tool with slots:
  Selection {select, directSelect}, Artboard {artboard}, Pen {pen, pencil,
  scissors}, Type {text}, Shapes {rectangle, roundedRectangle, ellipse, polygon,
  star, line}, Shape Builder {shapeBuilder}, Transform {rotate, scale}, Paint
  {gradient, eyedropper} and Navigate {hand, zoom}, with dividers between
  {Selection, Artboard}, {Pen…Shape Builder}, {Transform, Paint} and {Navigate}.
  Each slot button is named `tool:<first tool's raw>`, which
  AgentUiTests uses for `tool:select`. It shows the last-used tool
  (QSettings `toolSlot/<first raw>`), draws a corner triangle when it holds
  more than one tool, opens a QMenu flyout on right-click or a 400 ms press,
  and cycles on Alt-click. Choosing a tool by key shows it in its slot. The
  Basic/Advanced preset (QSettings `toolPreset`, default Advanced; switched from
  a context menu on the rail and View ▸ Toolbar) hides roundedRectangle,
  polygon, star, scissors, shapeBuilder, rotate, scale and gradient in Basic,
  but a hidden tool picked by key still shows in its slot. Rewrite
  `ContentViewTests::theRailHoldsEveryToolInGroups` and `aRailClickPicksTheTool`
  for slots.

## Left (checklist)

- [ ] `src/Document/VectorDocument+Artboards.cpp` (the helpers declared in the header)
- [ ] Codec v4, and the migration test
- [ ] Renderer, exporter and share defensive paths; `Share::selectionDocument` and AgentTools clear `artboards`
- [ ] EditorSession artboard API, active artboard and viewport compensation
- [ ] The Artboard tool (enum, keys, canvas drags, painting, cursor, icon, header, hint)
- [ ] Menus, context menus, the command palette (picks up menu actions itself) and the rename dialog
- [ ] Properties' Artboards list
- [ ] Export and share of the active artboard
- [ ] ScreenExport and ExportForScreensSheet
- [ ] Desk frames as artboards
- [ ] Rail flyouts and the Basic/Advanced preset
- [ ] Tests: two artboards export two PNGs named after them; Fit to Artwork
      Bounds hugs the art; v3 migration; the batch writes the expected
      files and suffixes (two assets at 1× and 2× give four files with `@2x`);
      a flyout opens on right-click and long-press, Alt-click cycles, the slot
      shows the last-used tool, which is saved, and every tool key still works;
      Basic hides the listed tools; undo names for every artboard edit
- [ ] docs/QOL-RESEARCH.md: mark P2-1, P2-10 and P2-12 ✓ with notes like P2-8's; README feature list
- [ ] Full suite: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DOMASTRATOR_WERROR=ON && cmake --build build -j1 && ctest --test-dir build -j2 --timeout 300`

## Exact next steps

1. Implement `VectorDocument+Artboards.cpp` and codec v4, and get `oma_core` building.
2. Do the renderer and exporter paths, then the EditorSession API with its
   Document tests (`tests/Document/ArtboardsTests.cpp`).
3. The Artboard tool and canvas painting (`tests/Canvas/ArtboardToolTests.cpp`).
4. ScreenExport with its IO tests (`tests/IO/ScreenExportTests.cpp`), then the sheet and the menus.
5. The flyouts (`tests/UI/ToolFlyoutsTests.cpp`), then fix ContentViewTests and MenusTests counts.
6. The Desk, the docs, the full suite, commit and push. Don't merge to main.
