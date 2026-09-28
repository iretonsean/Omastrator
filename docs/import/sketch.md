# Sketch (.sketch)

Part of the import-open brief. See `docs/import/quick-wins.md` for Inkscape
SVG, .svgz, HEIC/AVIF, PSD and Excalidraw, and `docs/import/penpot.md` for
Penpot.

## Decisions

- **Page → layer, artboard → `Artboard`, not a frame object.** SHARED.md's
  rule ("Multiple pages or artboards become multiple artboards") overrides
  OPEN.md's softer "artboard → frame (or artboard)": every Sketch artboard
  becomes a real `VectorDocument::artboards` entry (position, size,
  background), the same concept as an Illustrator artboard or a PDF page,
  not a `frame`-kind `VectorObject`. Omastrator's artboards are plain
  canvas regions with no object of their own (`VectorDocument::objectsOn()`
  finds an artboard's content by bounds, not by parentage), so an
  artboard's Sketch layers become ordinary objects, absolute-positioned to
  land inside that region. Each Sketch *page* becomes one Omastrator
  *layer* holding every artboard's content for that page, plus whatever
  loose (non-artboard) layers sit directly on it.
- **Pages are laid out left to right on one shared canvas**, since Sketch
  gives each page its own coordinate space but Omastrator has one. Each
  page's own artboards keep their relative positions; a running x-offset
  (that page's content width plus a gap) separates one page's artboards
  from the next page's, so nothing from two different pages overlaps.
- **A page with no real Sketch artboards gets one synthetic artboard**
  sized to its content's bounds, named after the page — matching
  SHARED.md's "everything becomes an artboard" rule for the common case of
  a simple file that never used Sketch's artboard tool.
- **`symbolMaster` becomes both an `Artboard` entry (so it has canvas real
  estate to look at) and a component group** (`VectorObject::component`
  set) holding its actual content, at the same position. `symbolInstance`
  becomes a group with `VectorObject::instance` set (`master`, `placement`
  and text overrides) and empty children: `Components::sync()`, called
  once after the whole document is built, fills them in from the master —
  exactly the mechanism the app already uses for its own components, so
  the importer doesn't reimplement copying or override application.
- **Override keys are resolved through `Components::keys()` itself**, not
  a hand-rolled path-naming scheme: after building a symbolMaster's
  subtree, the importer calls `Components::keys(document, masterID)` (the
  very function `Components::sync()` will call later) and keeps a map from
  each Sketch layer's own `do_objectID` to the key that walk gave it. A
  `symbolInstance`'s `overrideValues[].overrideName` (a Sketch layer id,
  optionally a "/"-joined chain for a nested symbol) is resolved through
  that map, so the keys an override sets are guaranteed to match the keys
  `Components::keys()` will look for when `sync()` runs — no risk of the
  two path-building rules drifting apart.
- **`symbolMaster`s build before anything else**, in a first pass over every
  page, so a `symbolInstance` on a page processed earlier or later in file
  order always finds its master already built (Sketch commonly puts a
  "Symbols" page last, after every page that uses it). Each artboard —
  real ones and symbol masters alike — remembers its own (page index,
  layer index) file position, and the final artboard list is sorted back
  into that order before `VectorDocument::setArtboards()` runs, so the
  *first* artboard (which sets `document.size`/`background`, and is what
  a fresh window fits to) is still whichever artboard actually comes first
  in the file, not whichever the build happened to construct first. Caught
  by opening a real two-page fixture in `build/omastrator`: without the
  sort, a file listing its content page first and its Symbols page second
  opened zoomed into a symbol's own tiny artboard instead of the content.
- **oval, polygon, star and shapePath share one geometry reader.** Sketch
  stores all four as the same generic `points[]` of curve points
  (`point`/`curveFrom`/`curveTo`/`cornerRadius`, fractional of the layer's
  frame), so `SketchImport::shapePathGeometry()` reads them uniformly
  instead of reconstructing an oval from `Shapes::ellipse()` or a star from
  `Shapes::star()` — more faithful to whatever the designer actually edited
  the curve into, and less code. Only `rectangle` gets special handling,
  since it alone maps onto `LiveRectangle` (a live, re-editable shape);
  every other kind becomes a plain baked `VectorPath`, matching how the
  document model itself only keeps rectangles live.
- **A node's smoothness comes from `hasCurveFrom`/`hasCurveTo`, not
  Sketch's `curveMode` integer.** `PathNode::smooth` is a single bool
  ("moving one handle mirrors the other's direction"), and Sketch's own
  enum (straight / mirrored / asymmetric / disconnected) doesn't map onto
  that one bit cleanly without documentation confidence in its exact
  integer values. A point with both curve handles extended reads as smooth
  either way in practice, so this sidesteps needing the enum at all.
- **A `shapeGroup`'s children combine one pair at a time** with
  `PathOperations::combine()`, each by its own `booleanOperation`, rather
  than calling `combine()` once with every path: the app's own `combine()`
  takes one operation for the whole stack, but Sketch allows a mixed
  union/subtract/intersect chain within one group, so the importer folds
  the result in one child at a time instead.
- **Shared colour assets become `DesignToken`s** (`document.tokens`), but
  fills don't get linked back to them (no `Paint::token` set) — Sketch's
  fill JSON doesn't carry an unambiguous swatch reference in the files this
  importer was tested against, so wiring usage would have been a guess.
  Shared layer and text styles (`layerStyles`/`layerTextStyles`) aren't
  read at all: every layer's `style` object already carries its fully
  resolved fields regardless of whether it follows a shared style, so
  *visual* fidelity doesn't need them — only the "linked style, edit once"
  convenience would, which is out of scope here.
- **Rotation direction is unverified.** `layerTransform()` rotates by
  Sketch's own `rotation` degrees with no sign flip, matching the
  Excalidraw importer's convention. It looks right in the manual
  `build/omastrator` smoke tests (a star rotated 15° visibly tilts), but
  there's no independent reference render of a real Sketch file to check
  the sign against; if a rotated shape ever comes in backwards, negate the
  angle in `layerTransform()`.

## What imports

Page → layer; artboard → `Artboard`; group → group; rectangle → a live
`LiveRectangle` shape (`fixedRadius` or per-point `cornerRadius`); oval,
polygon, star and shapePath → Bézier paths, curves included; shapeGroup →
one path, its children combined by their boolean operations; text →
`TextContent` with font, size, colour, per-run formatting (through
`attributedString`'s runs), alignment and line height, and fixed- vs
auto-width from `textBehaviour`; bitmap → a placed, scaled image;
symbolMaster → a component group; symbolInstance → an instance group with
its text overrides applied through `Components::sync()`; a layer with
`hasClippingMask` clips the layers that follow it, matching Sketch's own
mask semantics (not the app's "first child clips" one, which is why the
importer builds its own clip groups rather than reusing that convention
directly); fills, borders (with dash pattern, cap and join), opacity and
blend mode; shared colour assets → design tokens; locked and hidden
layers keep their state.

## What doesn't

- Shadows, inner shadows and blur (no model equivalent) — one warning
  each, per SHARED.md's rule.
- Image and pattern fills (`fillType` 2) — the model has no image-fill
  `PaintKind` — one warning.
- Angular gradients become radial (the closest of the two kinds the model
  has) — one warning.
- Rounded corners on anything other than a plain rectangle (oval,
  polygon, star, shapePath, or a rectangle inside a shapeGroup) — the
  model has no live-corner concept outside `LiveRectangle` — one warning.
- Type on a Path (`automaticallyDrawOnUnderlyingPath`) — one warning.
- Slices — one warning; they're an export artifact, not drawn content.
- Symbol overrides other than a text (`_stringValue`) override — fills,
  images and text-style overrides have no equivalent path in this pass —
  one warning naming the gap.
- A symbol instance resized independently of its master keeps the
  master's own size (position and rotation still follow the instance) —
  one warning when a resize is detected.
- Smart layout (Sketch's own auto-layout) isn't read at all; nothing maps
  it onto the model's `AutoLayout`. Not attempted: the fixtures this
  importer was built and tested against didn't exercise it, and the field
  layout wasn't confirmed against real files. A layer that used smart
  layout still imports as a plain, absolutely-positioned group — just
  without the auto-resize behaviour.
- Nested symbol overrides (an override chain longer than one id, for a
  symbol nested inside another symbol) resolve only their last segment
  against the *outer* symbol's own key map; a multi-level chain that needs
  the *inner* symbol's own keys isn't followed. Uncommon, and silently
  falls back to "no matching key, override dropped" rather than crashing.

## Report

All 12 `SketchImporterTests` pass: artboards and backgrounds, a rectangle
with `fixedRadius`, per-point corner radii overriding it, a shapeGroup's
boolean combine, text (font, size, alignment), colour-asset tokens, and a
symbol instance across two pages (master defined *after* its instance in
file order) getting the master's children with its own override applied.
`ProjectWorkspaceTests`/`ImageImporterTests`/other IO/UI suites still pass
unchanged (101/101 across the whole project via `ctest`).

Manually verified twice in `build/omastrator` (offscreen,
`OMASTRATOR_SNAPSHOT`), reading real zip files built with Python's
`zipfile` (DEFLATE-compressed, exercising ZipReader's zlib path, not just
the hand-built stored-only zips the automated tests use):
- A one-page file: a rounded rectangle, a bezier circle (`oval`), a
  rotated star, a shapeGroup boolean (notched square), and bold text —
  all rendered correctly, positioned and sized to match the source, with
  the missing-font banner correctly naming "Helvetica" (confirming
  `SketchImport`'s PostScript-name splitting worked: "Helvetica-Bold" →
  family "Helvetica", style "Bold").
- A two-page file (content page first, "Symbols" page second): both
  symbol instances rendered the master's card background and each
  showed its *own* overridden label text side by side, and the artboard
  the window opened fit to was the content page's, not the symbol's —
  the fix described above under "symbolMasters build before anything
  else."
