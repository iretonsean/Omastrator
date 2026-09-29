# Pages (design, 2026-09-28)

Phase 1 of BROWSER-FRAMES.md, and item 9 of FIGMA-AUDIT.md. A page is its own
canvas holding layers, artboards and guides, as Figma's pages are. It is
useful before workspaces exist, and phase 2 maps each page to a Hyprland named
workspace by its id, so **a page's id never changes and its name is unique in
its document**. Nothing here is built yet.

## 1. The model

Storage stays flat. A page is a tag on the three things that sit on a canvas:

- `struct Page { QUuid id; QString name; }` and `VectorDocument::pages`, in
  order. **Empty means one implicit page**, "Page 1", with a fixed id (as
  `allArtboards()` fixes the implicit artboard's id), so every document built
  in code today, and every old file, stays a valid one-page document.
- `VectorObject::page` (read on layers only; children are on their layer's
  page), `Artboard::page` and `Guide::page`. A null id means the first page.
- `VectorDocument::currentPage`: the page shown. Null means the first.
- `ensurePages()` makes the implicit page explicit and stamps its id on every
  null layer, artboard and guide. Every page operation calls it first, and so
  does `decode`. After it, reordering pages can't move untagged art.
- `insert()` of a top-level layer with a null page gives it `currentPage`.
- Every page has at least one artboard. New Page makes "Artboard 1" at the
  origin, the size of the current artboard. `size`/`background` keep mirroring
  the raw `artboards[0]` (the first page's first artboard), as now.

Why flat, and not a list of pages each owning its objects: ids stay global, so
everything that keeps a document consistent (components and their instances on
other pages, a "Components" page, tokens and modes, text threads, Find/Replace
Font, auto layout, export assets, the codec's id checks) works across pages
unchanged. Only code that shows or hits things by position has to ask which
page it's on, and most of it already goes through a few helpers:

- `layers()` returns **the current page's** layers; `allLayers()` returns every
  page's. The renderer, the Layers panel and `openLayer` go through `layers()`.
- `allArtboards()`, `artboardCount()`, `artboard(i)`, `artboardAt()`,
  `artboardBounds()`, `objectsOn()`, `artCenteredIn()` and
  `artboardDocument(i)` are **current page only**. Artboard indices are
  indices within the page, so `activeArtboard()` and every caller keep working.
  `allArtboards()` applies `size` to the raw first artboard, then filters.
  `artboardsOn(page)` serves the all-pages exports. Delete Artboard refuses a
  page's last artboard.
- `hitTest`, `hitTestAll`, `matching` (Select ▸ Same and the filters) and
  Select All are current page only. `artboardDocument(i)` and `croppedTo(ids)`
  return single-page documents (pages empty, page tags null).
- New: `pageOf(id)`, `isOnCurrentPage(id)`, `allPages()`, `pageIndex(id)`,
  `uniquePageName(base)`.

**The rule for every other walk over `objects` or `guides`:** code that finds
or shows things by position (canvas tools, smart guides, the pen's join search,
the frame under the press, rulers) filters with `isOnCurrentPage`. Code that
keeps the document consistent covers every page.

**The current page is view state, not an undo step** (as in Figma). It lives on
the document only so the renderer and hit tests see it without new parameters.
Switching pages records no history step, doesn't mark the file modified, and
emits `changed()` and a new `currentPageChanged(QUuid)` but never
`documentChanged()`, so autosave and the cloud upload don't fire. Because a
history snapshot is the whole document, **undo and redo return to the page the
step was made on**, which is what Figma does.

## 2. The file format

Additive keys in `DocumentCodec`:

- top level: `"pages": [{"id", "name"}]` and `"currentPage": id`;
- on layers, artboards and guides: `"page": id`.

Reading: a file without `pages` is one page with the fixed id, and every layer,
artboard and guide lands on it. A `page` that names no page goes on the first
page. A duplicate page id is dropped, and an empty or duplicate name is made
unique. `currentPage` naming no page means the first.

Writing: **a one-page document is still written as version 5**, so an older
build opens it exactly as before (it ignores the keys and loses only the page's
name). **A document with two or more pages is written as version 6.** An older
build refuses it with its existing "made by a newer Omastrator" message,
instead of stacking every page on one canvas and flattening them on save.

The clipboard format is unchanged. A copied layer carries its `page` key, and
paste gives it the current page (see `insert()` above), so pasting between
pages or documents lands where you're looking.

## 3. Editing

Each is one named `EditorSession` step (EditorSession+Pages.cpp):

| Call | Undo name | What it does |
|---|---|---|
| `addPage(name = {})` | New Page | After the current page, named `uniquePageName("Page")`, with one layer and one artboard. It becomes current. |
| `duplicatePage(id)` | Duplicate Page | After the original, "<name> Copy" (made unique). Layers, artboards and guides are copied with fresh ids, and parents and text threads are remapped inside the copy. Instances keep their main component. It becomes current. |
| `renamePage(id, name)` | Rename Page | Trimmed. An empty name is refused, and one already taken gets a number. |
| `deletePage(id)` | Delete Page | Never the last page. Its layers, artboards and guides go, as deleting those objects does now (export assets are pruned). If it was current, the next page becomes current, else the previous one. |
| `movePage(id, index)` | Reorder Pages | Changes only the order of `pages`. |
| `moveSelectionToPage(id)` | Move to Page | Keeps positions. Whole layers in the selection change page. Other objects go into the target page's layer with the same name as theirs, else its top unlocked layer, else a new "Layer 1". The view stays put and the selection empties. The status bar says "Moved to <page>". |

Not undo steps: `setCurrentPage(id)`, and `showPage(bool next)` (Next and
Previous Page, wrapping). Switching **commits** an interaction in progress and
ends inline text editing, as switching tools does. The session (never the
file) **remembers per page** the viewport, active artboard and selection, and
restores them on return; a first visit fits the page's artboards, artboard 0
active, nothing selected. **One history** covers the document: undo restores
the step's selection, its page, and that page's remembered viewport.

## 4. What each consumer sees

| Consumer | Sees |
|---|---|
| Canvas: drawing, labels, shadows, hit tests, smart guides, rulers and guides, pen, frame tool | The current page |
| Layers panel and its count, the layer drag-and-drop | The current page's layers |
| Properties' artboard list, the status bar | The current page. The status bar shows "Page 2 · Artboard 1" once there are two pages |
| PNG, JPEG and SVG export (File ▸ Export) | The current page's active artboard, as now. Figma exports what you're looking at. |
| **PDF export** | **One PDF page per artboard, across every page**, in page order then artboard order. This is Illustrator's multi-artboard PDF, and PDF import will round-trip it. It departs from Figma's "Export frames to PDF", which covers the current page only, because a PDF is how the whole document is handed off. It also fixes today's first-artboard-only PDF. |
| **Export for Screens** | **Every page**, as Figma's Export covers every page. The sheet lists artboards under a heading per page. Requests address artboards by id, not index. With two or more pages, files go in a folder per page (`<page>/<artboard>@2x.png`). Export assets on any page are included. |
| Share | Unchanged: the active artboard of the current page, or the selection. The scope text names the page when there are two or more. |
| Place (VectorFileImporter) | The placed file's first page, first artboard |
| Agent `document_get` | The current page's objects and artboards, plus `pages` (`[{id, name, current}]`). An optional `page` argument (an id, a name, or `"all"`) picks another. |
| Agent `render`, `align` to artboard | The current page, or the `page` passed. `align` uses the active artboard, fixing today's artboard 0. |
| Agent `page` tool (new) | `action`: `add`, `rename`, `duplicate`, `reorder`, `move_objects`, `show`. Each is a named step through the agent's usual edit path. There is no `delete`: losing a page is the designer's call. |
| Clipboard, Figma paste | The current page (section 2) |
| Command palette's `ask()`, Anywhere, System, Design | Their own single-page documents, or the current page, through `layers()`. The builder checks each `objects` walk against the rule in section 1. |
| Components, tokens, fonts, auto layout, text flow, the codec | Every page |

## 5. Importers

Phase 1 is shippable with the importers unchanged (they lay source pages out
as side-by-side artboards on one page, which is dated but correct). Making them
produce pages is **the last commits of phase 1**, one per importer, each
droppable. Each already makes one layer per source
page, so the change is small: `ensurePages()`, a `Page` per source page, tag
that page's layer and artboards, and drop the x offset.

- PDF (`PdfImporter::buildDocument`): a page per PDF page, each with one
  artboard. This round-trips the PDF export above.
- Figma (`FigmaImporter+Mapper.cpp` `map`): Figma's pages, by name.
- Sketch (`SketchImporter` `Builder::build`): Sketch's pages. The Symbols page
  stays a page.
- Penpot (`PenpotImporter` `Builder::build`/`buildPage`): Penpot's pages.

The current page after import is the first.

## 6. UI

**The Pages list sits at the top of Layers**, above the layer tree, using
PANELS.md's density:

- A `PanelSection`-style heading, "Pages", with a 24 px **+** (New Page). It
  folds, remembered (`layers/collapsed/pages`), and folded its summary is the
  current page's name (PANELS.md, C).
- 24 px rows; the current page's has the selection fill. Up to 5 rows, then it
  scrolls. Always shown, even with one page, as in Figma: it costs 24 px and
  it's where New Page lives. Click switches, double-click renames inline,
  dragging reorders.
- Context menu on a row: New Page, Duplicate Page, Rename…, Delete Page
  (disabled on the last page; no prompt, since it's one undo step, like
  deleting a layer).
- Layer rows' and the canvas's context menus gain **Move to Page ▸**, listing
  the other pages. It appears only with two or more pages and a selection.
  Dragging a layer onto a page row is a later addition, not phase 1.

**Menus:** an **Object ▸ Pages** submenu beside Object ▸ Artboards: New Page,
Duplicate Page, Rename Page…, Delete Page, Next Page, Previous Page, and Move
to Page ▸. The gates go in `Menus+Gates.cpp`.

**Keyboard:** Figma binds none. Ours: **Alt+PageDown / Alt+PageUp** for Next
and Previous Page, matching Shift+PageDown / Shift+PageUp for artboards. Both
are free today. Add them to SHORTCUTS.md and the Keyboard Shortcuts sheet. In
phase 2, Super+number reaches pages through their workspaces.

**Ctrl+K:** the menu actions come in through `gatherMenus()` by object name
(`newPage`, `duplicatePage`, `renamePage`, `deletePage`, `nextPage`,
`previousPage`). `gatherRest()` adds one command per page, "Go to Page: <name>"
(`page:<id>`), and with a selection, "Move to Page: <name>" (`moveToPage:<id>`).
Both have the keywords "page" and "canvas".

## 7. Tests and build plan

Tests (new suites, or cases in the suite named):

- **Document/PagesTests, the model:** the implicit page and its fixed id;
  `ensurePages` stamping; `layers()`, `allArtboards()`, `hitTest`, `matching`
  and Select All kept to the current page, while `allLayers()` and `find()`
  span every page; the renderer draws only the current page (a pixel check);
  a top-level `insert` takes the current page; `artboardDocument` and
  `croppedTo` give single-page documents; `size` follows the first page's
  first artboard.
- **Document/PagesTests, the session:** each section 3 operation is exactly
  one step with its undo name; undo and redo return to the step's page;
  `setCurrentPage` adds no step, leaves `isModified` false and emits no
  `documentChanged`; viewport, active artboard and selection are remembered
  per page; the last page can't be deleted, and deleting the current one
  moves to its neighbour; duplicate remaps ids and parents and keeps names
  unique; Move to Page keeps positions, matches layer names, moves whole
  layers and makes a layer when the target has none; switching commits an
  interaction; an instance on page 2 follows its main on page 1.
- **IO/ProjectStoreTests:** pages, the current page and page tags round-trip;
  a version-5 file loads as one page; one page writes version 5, two write
  version 6; an unknown page tag goes to the first page; duplicate page ids
  and names are repaired.
- **IO/DocumentExporterTests:** the PDF has one page per artboard across
  pages; PNG and SVG export the current page.
- **IO/ScreenExportTests:** every page, a folder per page, artboards by id,
  assets from another page.
- **UI/NativeLayerListTests** (or a new PagesListTests): rows and the current
  row, click switches, inline rename, the drag reorder is one step, the
  context menu's gates, the folded summary.
- **UI/CommandPaletteTests** (Go to Page, Move to Page), **UI/MenusTests** (the
  gates), **UI/KeyboardShortcutsTests** (Alt+PageDown and Alt+PageUp).
- **Agent/AgentToolsTests:** `document_get`'s pages and `page` argument, the
  `page` tool's actions as undo steps, and no `delete`.
- **Importers:** each multi-page fixture now makes pages (PdfImporterTests,
  FigmaImporterTests, SketchImporterTests, PenpotImporterTests;
  PdfRoundTripTests for export then import).

Build plan, one commit each, with its tests:

1. **Model:** `Page`, the page tags, `currentPage`, `ensurePages`; the
   current-page `layers()` and artboard helpers, `allLayers()`; hit tests,
   `matching`, Select All; the renderer.
2. **Codec:** the keys, the reading repairs, version 5 or 6 by page count.
3. **Audit** every other `objects`/`guides` walk against section 1's rule:
   canvas, smart guides, pen, frame tool, rulers, Anywhere, System, `ask()`.
4. **EditorSession+Pages.cpp:** the operations, `setCurrentPage`/`showPage`,
   `currentPageChanged`, the per-page view memory, undo landing on its page.
5. **The Pages list** in LayersPanel: heading, rows, rename, reorder, menu.
6. **Menus, shortcuts, palette:** Object ▸ Pages, Move to Page ▸ in the
   context menus, Alt+PageUp/PageDown, the gates, Ctrl+K's page commands,
   SHORTCUTS.md.
7. **Status bar and Properties:** the page name in the status bar; the
   Properties artboard list checked to be current-page only.
8. **Exports:** PDF across pages; Export for Screens across pages (ids,
   per-page folders, the grouped sheet); Share's scope text.
9. **Agent:** `document_get`'s pages and `page`, `render`/`align` with `page`,
   the `page` tool, and docs/AI-DESIGN.md's tool list.
10. to 13. **The PDF, Figma, Sketch and Penpot importers** make pages, one
    commit each.
14. **Docs:** FIGMA-AUDIT.md's Pages row (built), and this file's status line.

Commits 1 to 9 are a shippable phase 1; 10 to 13 can land separately. Phase 2
builds on `currentPageChanged`, `Page::id` and `Page::name`.
