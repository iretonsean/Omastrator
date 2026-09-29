# Figma import: paste, link and .fig (report, 2026-09-28)

Branch `feat/import-figma`. Read `SHARED.md` and `FIGMA.md` in
`.omastrator-import-briefs/` first; this doc is the report those briefs ask
for, plus the running decision log.

## What imports

All three sources — paste, a `figma.com` link (REST) and a `.fig` file —
normalize into the same tree and go through one mapper
(`FigmaImporter+Mapper.cpp` and its `+Part.cpp` files), so a feature mapped
once works everywhere:

- **Frames and sections** → `ObjectKind::frame` (fills, strokes, corner radii —
  four independent corners when Figma's own `rectangleCornerRadiiIndependent`
  is set — and `clipsContent`). Auto layout (direction, gap, padding,
  primary/counter alignment, wrap, hug/fixed/fill sizing) maps onto
  `AutoLayout`/`LayoutItem` (docs/AUTO-LAYOUT.md).
- **Groups** → `ObjectKind::group`.
- **Rectangles** → a live `LiveRectangle` (per-corner radius, corner style
  stays round — Figma's "corner smoothing" has no equivalent and is left off
  with a warning).
- **Ellipses, stars, regular polygons, lines** → plain paths, built from
  Figma's own vector geometry when Kiwi's `vectorData.vectorNetworkBlob` or
  REST's `fillGeometry` (with `?geometry=paths`) has it, else Omastrator's own
  `Shapes::` generators for the parametric ones.
- **Vectors** → the same vector-network/`fillGeometry` decode.
- **Boolean operations** → flattened to one plain path through
  `PathOperations::combine`, walking the operation's own children (not the
  blob), since the model has no live boolean node (docs/FIGMA-AUDIT.md).
  Warned once.
- **Components and instances**: a Kiwi `SYMBOL` (REST `COMPONENT`) becomes
  `ComponentInfo`; a component set (Kiwi `FRAME` + `isStateGroup`, REST
  `COMPONENT_SET`) keeps its variants' `Prop=Value, Prop2=Value2` naming
  convention for `ComponentInfo::variant`. An `INSTANCE` becomes a group with
  `InstanceInfo` and *no mapped children of its own* — `Components::sync`
  (called explicitly at the end of `FigmaMap::map`, and again by
  `EditorSession::loadDocument`) rebuilds them from the master plus the
  overrides resolved below.
- **Instance overrides**: Figma's `symbolOverrides` key each change by a
  `guidPath` into the *master's* guid space. The mapper keeps a
  `Context::objectFor` table (every mapped node's Figma guid → its `QUuid`),
  resolves the override's target guid through it, then looks up that
  `QUuid`'s name path with `Components::keys(document, masterUuid)` to build
  the `InstanceOverride` map the way Omastrator's own model wants it. Only
  fill (first solid paint), stroke (first solid paint), text and visibility
  are carried over — the fields `InstanceOverride` has.
- **Text**: character and paragraph formatting, `TextContent::area` for
  point/fixed/auto-height type (Figma's `textAutoResize`), and per-character
  runs built from `characterStyleIDs`/`styleOverrideTable` (Kiwi) or
  `characterStyleOverrides`/`styleOverrideTable` (REST, a `{id: TypeStyle}`
  map reshaped into the same run-table form before mapping).
- **Paint**: solid, linear and radial gradients (angular and diamond
  approximated as radial, warned once); gradient handles/transform converted
  to Omastrator's fraction-of-bounds `Paint::start`/`end`. A lone `IMAGE` fill
  on a rectangle/frame with no children and resolved bytes becomes its own
  `ObjectKind::image` object (images are only their own layers in this model
  — docs/FIGMA-AUDIT.md); anything else with an image paint drops it with a
  warning.
- **Strokes**: weight, align, cap, join, dashes. Per-side stroke weights
  aren't in the model; the one weight is used all round, warned once.
- **Pages** → pages (docs/PAGES.md), one per Kiwi `CANVAS`/REST canvas page,
  each with an artboard sized to its own content's bounds, at the origin. A
  file with one page stays a plain one-page document.
- **Multi-page or single-node REST imports** both land as a new, unsaved tab
  (`ProjectWorkspace::openDocument`), the same as File ▸ Open.

## What's left out (warned, not silently dropped)

- **Effects** (drop/inner shadow, layer/background blur): the document model
  has none yet (docs/FIGMA-AUDIT.md). Warned once per document.
- **Masks**: `node.mask`/`isMask` isn't restructured into a clip group yet.
  Warned once; the masked layers still show, just without clipping.
- **Corner smoothing**, **per-side stroke weights**, **layout grids**,
  **prototyping links**, **pattern/emoji fills**: no model equivalent: left
  out, most with a warning (patterns and emoji currently fold into the
  generic "pattern fills were left out" message from `mapPaint`).
- **Lowercase and title-case** text (`textCase`): no equivalent `TextCase`
  value; comes in as regular text, warned once.
- **Vertical text alignment** (center/bottom in a fixed box): text always
  sits at the top of its box; warned once.
- **Component properties** (boolean/text/instance-swap) and the newer
  per-instance `componentPropertyDefinitions`/`componentProperties`: only the
  classic `Name=Value` variant-naming convention is read.
- A vector network blob's stray/open segments (not part of any region's
  loop): left out of the path, warned.

## Decisions

- **One shared mapper, Figma's own (Kiwi) field names as the common
  vocabulary.** Rather than a strongly-typed intermediate struct, the shared
  tree (`FigmaMap::Tree`/`Node`, `FigmaMap.h`) keeps each node as a
  `QVariantMap` keyed by Figma's own Kiwi field names (`stackMode`,
  `fillPaints`, `rectangleCornerRadiiIndependent`, …), which the Kiwi
  container already produces field-for-field from its embedded schema. The
  REST adapter (`FigmaImporter+Rest.cpp`) translates the REST API's
  different names (`layoutMode` → `stackMode`, `constraints.horizontal`'s
  `LEFT`/`RIGHT` → `MIN`/`MAX`, a `TypeStyle` → `fontName`/`fontSize`/…, REST's plain-number `letterSpacing` → Kiwi's `{units,value}` shape) once, at
  the door, so the rest of the mapper never checks which source a node came
  from. This matches FIGMA.md's "normalising both into one internal node
  model first" while reusing `QVariantMap` access helpers (`str`/`num`/
  `boolean`/`list`/`map` in `FigmaMapper.h`) instead of a parallel struct
  hierarchy.
- **Boolean operations are recombined from their children's own geometry**
  (rectangle/ellipse/vector paths, walked and unioned/intersected/subtracted
  with `PathOperations::combine`) rather than read from a flattened blob.
  Figma's file keeps both; recombining from the live children is more robust
  to a boolean whose blob is stale or missing, and it's what "as native as
  the model allows" means when there's no live boolean node to hand off to.
- **Instances never map their own realized children.** Figma's file *does*
  include an instance's full child subtree (mirroring the master), but
  `EditorSession::loadDocument` calls `Components::sync` immediately on
  every load, which throws that subtree away and rebuilds it from the master
  plus declared overrides. Mapping it would be wasted work and a trap for
  anyone reading the code expecting it to survive; the mapper skips an
  instance's own `nodeChanges` children entirely and leans on `sync` plus
  the guid-path override resolution above.
- **Pages become pages,** each with an artboard sized to its own top-level
  content's bounds (falling back to 800×600 when a page is empty), the
  content moved to the origin. Figma's pages are independent canvases with
  unrelated coordinate spaces, and so are ours now. A file with one page is a
  plain one-page document (its artboard is named after the page).
- **A lone `IMAGE` fill on a childless rectangle or frame becomes an actual
  image object**, matching how images are represented everywhere else in
  this model (their own layer, never a fill). Any other placement (mixed
  fills, a non-rectangular shape, a frame with children) falls back to
  "left out, warned" rather than guessing at a crop.
- **zlib is a public dependency of `oma_io`,** not just a private
  implementation detail, because ZipReader and the Kiwi container both need
  it and test fixtures build their own compressed archives directly
  (`tests/IO/FigmaKiwiFixtures.h`). zstd is optional (`pkg_check_modules`);
  without it, a zstd-compressed Kiwi chunk fails to decompress and the
  import throws a plain `FileError` pointing at Import from Figma Link…
  instead.
- **The REST fetch itself lives in the UI layer** (`FigmaLinkSheet`), not
  `oma_io`: `FigmaImporter::parseRestFile` takes JSON bytes and returns a
  `VectorDocument`, with no network dependency, so it's fully unit-testable
  offline. `oma_io` doesn't link `Qt6::Network`.
- **The personal access token** lives in `~/.config/omastrator/figma.json`,
  mode 0600, via `FigmaImporter::Token` (never in an `.omai`). Preferences
  gets a "Forget Access Token" button; the link sheet asks for one inline,
  the first time, with a link to where Figma issues it. A typed token is
  saved only after Figma accepts it (a mistyped one is never stored behind a
  hidden field), and `Token::save` returns false when it can't write; the
  import then warns that the token was used once only. The token file is
  0600 from the moment it's written (set on the temporary file, before the
  rename), in a folder created 0700. Requests use
  `SameOriginRedirectPolicy`, so the token can't follow a redirect off
  `api.figma.com`.
- **Hostile input** ends in a `FileError` or a warning, never a crash. Kiwi
  counts are checked against the bytes left before anything is allocated,
  decompressed output is capped at 256 MB (a zstd frame that claims more, and
  a deflate bomb, both fail), Kiwi nesting stops at 64 levels, the mapper
  stops at 256 levels ("Layers nested deeper than 256 levels were left
  out."), star and polygon point counts clamp to 3..1000, and
  `fromKiwiBytes` turns a `std::bad_alloc` into a `FileError`. Tests:
  `tests/IO/FigmaHostileTests.cpp`.
- **Paste** has a cheap `recognises` check (the marker in the first 16 KB of
  the HTML), used by `canPaste()` on every menu refresh, and a full `read`
  that only Paste runs. What a paste leaves out reaches the user through
  `EditorSession::pasteLeftOut`, shown by `ProjectWorkspace`.
- **Several pages** each sit inside their own artboard on their own page
  (a page's content is moved to its artboard's origin). An instance on one
  page of a component on another stays on its own page.

## Tests (`tests/IO/FigmaImporterTests.cpp`, `tests/IO/ZipReaderTests.cpp`)

No real Figma export was available, so `tests/IO/FigmaKiwiFixtures.h` is a
small in-test Kiwi encoder (a schema builder mirroring `decodeBinarySchema`,
and `KiwiWriter` mirroring `FigmaKiwi::ByteReader`) plus a minimal zip writer,
both built against the real wire format `FigmaKiwi.cpp` decodes — not a
mocked shortcut. One hand-built fixture exercises, end to end, through
`FigmaImporter::parse`: a page becoming an artboard, an auto-layout frame
with a fill and rounded corners, a plain rectangle's fill, styled text runs
(a bold, larger run via `characterStyleIDs`/`styleOverrideTable`), a vector
network blob decoding into a closed triangle, and a component instance whose
override changes a nested descendant's fill (confirming both the guid-path
override resolution and that `Components::sync` rebuilds the instance from
its master). Separately: `.fig` as a zip (`canvas.fig` inside), paste's
`(figmeta)`/`(figma)` HTML-comment decoding, `parseLink` against both current
and legacy Figma URL shapes plus `?node-id=`, the token store's round trip
and file permissions, a hand-written REST `GET /v1/files/:key` JSON fixture
(auto layout, fills, through `parseRestFile` — not a real network call), and
ZipReader's stored/deflated/multi-entry/garbage-input handling on its own.

## What needs checking against real Figma data

Everything above was built and tested against the wire format and the public
REST API's documented shapes (github.com/figma/rest-api-spec's OpenAPI-
generated TypeScript, and Netflix's fig2sketch as a field-name cross-check
for the *undocumented* Kiwi container — FIGMA.md warns the .fig format is
undocumented and changes), never against a file Figma's own app produced.
Before trusting this on real work, check:

- **A real `.fig` file, both shapes** (raw fig-kiwi, and the zip with
  `canvas.fig`/`images/`): confirms the container framing, and whether the
  live schema still calls its root message type `Message` and its node/guid/
  parentIndex fields what this code assumes.
- **A real paste** from Figma's desktop or web app: confirms the clipboard's
  `text/html` really carries `(figmeta)`/`(figma)` HTML comments the way
  FIGMA.md describes, and that `isFigmaClipboardHtml`'s cheap substring check
  doesn't collide with anything else Figma puts on the clipboard.
- **A real personal access token and REST fetch**, including a file with
  more than one page, a component set with several variant properties, and
  `?node-id=` on a nested node — confirms the 403/404/429 error mapping
  against Figma's actual responses, and that `/v1/files/:key/nodes` really
  wraps each id under `{"document": …}`.
- **The vector network blob's exact byte layout**: this was reverse-engineered
  (via fig2sketch, itself reverse-engineered) rather than read from a spec.
  In particular, whether a loop's segment indices ever need reversing (this
  code assumes they don't) matters for anything beyond simple, one-directional
  outlines — a real boolean-heavy or hand-edited vector shape is the sharpest
  test.
- **Real instance overrides** on a multi-level nested component (an instance
  inside an instance, or an override several levels deep) — the guid-path
  resolution is tested with one level of nesting only.
- **Real auto layout edge cases**: `SPACE_EVENLY`/`SPACE_BETWEEN` primary
  alignment, wrap, and a frame that's simultaneously filling its parent on
  one axis and hugging on the other.
