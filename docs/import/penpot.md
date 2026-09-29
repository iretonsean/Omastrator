# Penpot (.penpot)

Part of the import-open brief. See `docs/import/quick-wins.md` for Inkscape
SVG, .svgz, HEIC/AVIF, PSD and Excalidraw, and `docs/import/sketch.md` for
Sketch.

Supports Penpot 2.x's `.penpot` export (a zip of JSON, the "v3 binfile"
format from "Download file"). The older transit-based v1 binary format is
refused with a message asking to re-export from a current Penpot.

## The format, from Penpot's own source

Confirmed by reading `penpot/penpot`'s actual Clojure source (not blog posts
or guesses), specifically `backend/src/app/binfile/v3.clj` and
`common/src/app/common/types/{shape,fills,color,stroke,text,path/{impl,segment},shape/{layout,shadow,blur},component}.cljc`
at `develop`, 2026-09.

- **Zip structure**: `manifest.json` (`{type: "penpot/export-files", files: [{id, name, features}], relations, external-libraries}`),
  `files/<file-id>.json` (the file record), `files/<file-id>/pages/<page-id>.json`
  (the page record *without* its shapes), `files/<file-id>/pages/<page-id>/<shape-id>.json`
  (**one JSON file per shape** — Penpot's canonical form is a flat id→shape
  map per page, exactly like `VectorDocument::objects`, so each shape is
  written as its own zip entry rather than nested inside the page), plus
  `media/`, `components/`, `colors/`, `typographies/`, `tokens.json` and a
  top-level `objects/` for binary asset bytes.
- **JSON keys are camelCase**, written by `json/write-camel-key`
  (`v3.clj:236`) from Clojure's own kebab-case keywords: `:fill-color`
  becomes `"fillColor"`, `:parent-id` becomes `"parentId"`, and so on
  throughout. String *values* that were already plain strings in Clojure
  (shape `type`, text node `type`) are untouched.
- **Coordinates are absolute (page) space**, not fractional or
  frame-relative: `x`, `y`, `width`, `height` and `selrect` are the shape's
  own unrotated bounding box in page coordinates, path `content`'s
  `move-to`/`line-to`/`curve-to` points are absolute page coordinates too,
  and `transform` is a `{a, b, c, d, e, f}` affine matrix in the exact
  order `QTransform(a, b, c, d, e, f)` expects.
- **A shape's own base attrs**: `id`, `name`, `type` (`frame`, `group`,
  `bool`, `rect`, `circle`, `path`, `text`, `image`, `svg-raw`), `x`, `y`,
  `width`, `height`, `rotation` (degrees), `flipX`, `flipY`, `parentId`,
  `frameId`, `shapes` (child ids, frame/group/bool), `fills`, `strokes`,
  `opacity`, `hidden`, `locked`, `blendMode`, `r1`..`r4` (per-corner
  radius, valid on rect *and* frame), `shadow` (array, max 1), `blur`,
  `componentId`/`componentFile`/`componentRoot`/`mainInstance`/`shapeRef`.
- **Fill**: `{fillColor, fillOpacity, fillColorGradient, fillImage}` —
  exactly one of the three set. Gradient: `{type: "linear"|"radial",
  startX, startY, endX, endY, width, stops: [{color, opacity, offset}]}`.
  Stroke mirrors this exactly (`strokeColor`/`strokeColorGradient`/
  `strokeImage`), plus `strokeWidth`, `strokeStyle` (`solid`/`dotted`/
  `dashed`/`mixed`), `strokeAlignment` (`center`/`inner`/`outer`),
  `strokeCapStart`/`strokeCapEnd`.
- **Path content**: an array of `{command: "move-to"|"line-to"|"curve-to"|"close-path",
  params: {x, y, c1x, c1y, c2x, c2y}}` (curve control points optional on
  `curve-to`, everything absolute page coordinates).
- **`bool` shapes carry their own already-combined `content`**, the same
  shape as a path's — Penpot computes the boolean result once and stores
  it, rather than recomputing it from `shapes` (its child shape ids) on
  every read.
- **Flex layout**, on a frame: `layout: "flex"` (or `"grid"`, unsupported —
  see below), `layoutFlexDir`, `layoutGap: {rowGap, columnGap}`,
  `layoutPadding: {p1, p2, p3, p4}`, `layoutJustifyContent`,
  `layoutAlignItems`, `layoutAlignContent`, `layoutWrapType`. A child's own
  `layoutItemHSizing`/`layoutItemVSizing` (`fill`/`fix`/`auto`) and
  `layoutItemAbsolute`.
- **Text content**: a tree, `{type: "root", verticalAlign, children:
  [{type: "paragraph-set", children: [{type: "paragraph", textAlign,
  textDirection, children: [{text, fontFamily, fontSize, fontWeight,
  fontStyle, fills, letterSpacing, lineHeight, textDecoration,
  textTransform}]}]}]}`. `fontSize`, `lineHeight` and `letterSpacing` are
  stored as **strings** ("14", "1.2"), not numbers.

## Decisions

- **`bool` shapes import as a plain path from their own `content`**, not
  recombined from their `shapes` children through
  `PathOperations::combine()` the way Sketch's `shapeGroup` is. Penpot
  already stores the finished boolean result on the shape itself, so
  redoing the combine would be redundant work that risks a slightly
  different result from what the designer actually saw; the child shapes
  named in `shapes` aren't built as separate visible objects (they'd
  duplicate the same geometry the parent's `content` already draws).
- **Components are synced on import, and copies keep their changes as
  overrides.** Opening a document runs `Components::sync()`, which rebuilds
  every copy's children from its main component, so the importer sets
  each main's and copy's `placement` (its top-left corner: Penpot's
  coordinates are absolute) and records what a copy changed (text, fill,
  stroke, visibility, matched by layer name path) as `InstanceOverride`s
  before the importer's own sync. Layers a copy added, or size changes, follow
  the main component, with one warning. A copy's `componentId` is the
  component's id, which the main shape carries too; `shapeRef` and a
  componentId equal to the main shape's id are fallbacks.
- **Hostile files:** each shape id is built once (a frame listing itself, or
  a shape under two parents, warns once), the zero id is never a child, and
  nesting stops at 256 levels.
- **Grid layout gets one warning and imports as a plain frame** (its
  children keep their absolute positions, not the grid placement): the
  document model's `AutoLayout` is a single row/column flow (matching
  flex) with no grid concept, and building a faithful grid equivalent
  isn't attempted.
- **Gradient coordinates are assumed absolute (page-space), converted to
  the model's own 0–1-of-bounding-box fractions** by subtracting the
  shape's own `x`/`y` and dividing by `width`/`height`. The schema confirms
  `startX`/`startY`/`endX`/`endY` are plain numbers with a separate
  `width` (a radius-like field, not itself a coordinate), which only makes
  sense as absolute coordinates, not fractions — but this wasn't
  independently confirmed against a real exported gradient, so a
  gradient that lands in the wrong place should be rechecked here first.
- **Rotation only** (no shear/skew) is read from a shape's own `rotation`
  degrees field, ignoring the separate `transform` matrix — the same
  simplification as the Sketch and Excalidraw importers, and for the same
  reason: real files were built and tested with plain rotation, not skew,
  so there was nothing to verify a fuller transform reading against.
- **`svg-raw` shapes are left out with a warning**, not parsed: Penpot can
  embed arbitrary raw SVG content (`{tag, attrs, content}` or a bare
  string) inside a shape, and reading it faithfully would mean reusing
  most of `SvgImporter` from inside a completely different importer.
  Out of scope for this pass.
- **Images are found by a defensive scan of the zip's `objects/` entries**
  rather than by threading the shape's `metadata.id` through a confirmed
  `media/<id>.json` → `objects/<storage-id>.<ext>` chain: the exact
  cross-reference between an image fill/shape's media id and its storage
  object id wasn't confirmed from source in the time available. The
  importer instead looks for any `objects/` entry whose name contains the
  shape's own media id and decodes whichever one is found; if that fails,
  it warns instead of guessing further.
- **A file with several pages makes several pages** (docs/PAGES.md), each
  keeping its own coordinates (they used to overlap on one canvas); a
  one-page file stays a plain document. The first page shows.
- **A top-level board becomes its own `Artboard`; loose top-level shapes
  (outside any board) share one synthetic `Artboard` sized to their own
  combined bounds.** Unlike Sketch, where content nearly always sits
  inside an artboard, floating shapes directly on a Penpot page are
  ordinary and common, so the "no real board → one synthetic artboard"
  fallback that works for Sketch would silently lose them whenever a page
  *also* had even one real board. Both kinds of artboard keep the file's
  own top-level order (by each item's position in the page's root
  `shapes` list, the same file-order sort Sketch's importer uses for its
  own artboards) — caught by the same kind of manual check that caught
  Sketch's version of this bug: a page with a small flex-layout board
  *and* several loose shapes before it in the file opened fit to the tiny
  board instead of the actual content, twice, before both the "loose
  shapes need a home" gap and a second file-order bug (real boards were
  using a separate "boards seen so far" counter instead of their actual
  file position) were fixed.
- **No parent transform is composed into a child's placement at all** —
  a shape's own `x`/`y`/`rotation` place it in absolute page space by
  themselves, full stop, whether it's a top-level shape or nested three
  boards deep. An earlier version of this importer built each shape's
  placement as `shapeTransform(ownRect, ownRotation) * parentCTM`,
  mirroring the Sketch and Excalidraw importers' parent-relative
  composition — wrong for Penpot, since (per the source-confirmed schema
  above) coordinates are absolute at *every* nesting level, not relative
  to the parent the way Sketch's `frame`/Excalidraw's `x,y` are. That
  double-applied every ancestor's position on top of already-absolute
  coordinates, compounding with depth: a top-level rotated-0° path landed
  offset by its own `x,y` (confirmed with a path at `x:340,y:30` landing
  at `340,60` — no, at `680,60`, exactly double), and a board's child
  landed offset by the board's own position on top of its own already-
  correct one. Fixed by dropping the parent-transform parameter
  everywhere: `buildRect`/`buildCircle`/`buildFrame`/`buildText`/
  `buildImage` (whose geometry is built in local `(0,0)`-`(w,h)` space)
  place themselves with `shapeTransform(ownRect, ownRotation)` alone, and
  `buildPath`/`buildBool` (whose `content` points are already absolute)
  use a rotation-only transform, `rotateAboutCenter()`, since translating
  already-absolute points again would reintroduce the same bug. Caught by
  an independent review pass, not the manual smoke tests run while
  building this importer (which happened to use fixtures that made the
  bug invisible: a path at the page origin, and nested shapes given
  parent-relative test coordinates instead of Penpot's real absolute
  ones) — both `pathContentIsAlreadyAbsoluteNotRelativeToTheShapesOwnXY`
  and `nestedShapeKeepsItsOwnAbsolutePosition` in the test suite exist
  specifically to keep this fixed.

## What imports

Board → an `Artboard` (top-level) and a `frame`-kind `VectorObject`
(always, since boards can nest); flex layout → `AutoLayout` (direction,
gap, padding, justify/align); group → group; rect and frame corner radii
(`r1`..`r4`) → a live `LiveRectangle`; circle → an ellipse path; path and
bool → a Bézier path straight from `content`'s move-to/line-to/curve-to/
close-path commands; text → font family, size, weight/style→face,
alignment, colour and letter spacing, paragraph by paragraph; image →
a placed, scaled bitmap; a main component → a component (`set` named
after it); a component copy → an instance, keeping the geometry the file
already gives it; fills and strokes, including gradients (converted to
the model's own bounding-box fractions), dash style and stroke alignment;
opacity, blend mode, locked and hidden state; multiple pages, laid out
left to right like Sketch's.

## What doesn't

- Shadows and blur (no model equivalent) — one warning each.
- Image and pattern fills (`fillImage`) — one warning; the model has no
  image-fill `PaintKind`.
- Grid layout — one warning; a board using it imports as a plain frame,
  its children keeping their absolute position rather than a grid
  placement the model has no equivalent for.
- `svg-raw` shapes (embedded raw SVG) — one warning; out of scope for
  this pass (see Decisions).
- Component instance overrides aren't read as `InstanceOverride`s (no
  equivalent extraction was attempted — see Decisions): visually this
  doesn't matter, since an instance's own shapes already carry whatever
  overrides were made, but the app's Components panel won't show them as
  *overrides* the way it does for a document built in the app itself.
- Files exported with linked libraries (`include-libraries`/
  `embed-assets`) list more than one file in their manifest; only the
  first (the file itself) is imported, with one warning. Not attempted:
  building and testing against a real multi-library export was out of
  reach without a running Penpot instance to export one from.
- The `transform` matrix (skew) is ignored in favour of the simpler
  `rotation` field — see Decisions.

## Report

All 16 `PenpotImporterTests` pass: manifest detection, a board (frame +
artboard + background fill), a rectangle with per-corner radii and a
linear gradient fill (start/end converted to the model's fractions),
path content (curve-to's control points read correctly, a closed
contour), a `bool` shape using its own baked `content` rather than
recombining its children, flex layout → `AutoLayout`, grid layout's
warning, text (font, size, weight → face, alignment), a component root
paired with an instance that keeps its own file-given position rather
than being repositioned by a `Components::sync()` rebuild, a component
copy built *before* the main component it refers to still resolving,
and the absolute-position regression tests described under Decisions
(a rotated-0° path away from the origin, and a nested shape under a
positioned board). The full project's test suite still passes unchanged
(102/102 via `ctest`).

Manually verified in `build/omastrator` (offscreen, `OMASTRATOR_SNAPSHOT`)
twice: once reading a real DEFLATE-compressed zip built with Python's
`zipfile` in the actual manifest/files/pages/shapes-per-file structure
this importer expects (a rounded, stroked rectangle; a linear-gradient
circle; a path shape; text with the missing-font banner correctly naming
"Work Sans"; and a flex-layout board with two coloured chips), and a
second time after the parent-transform fix above, confirming a shape
away from the page origin now renders in the right place both at the
top level and nested inside a positioned board — the second round is
what actually caught the parent-transform bug, since the first round's
fixtures happened to avoid triggering it. Three real bugs were caught
and fixed this way: loose top-level shapes losing their artboard
entirely when a board was also present, the artboard order not
following the file once that was fixed, and the parent-transform
double-positioning bug (all described under Decisions above).
