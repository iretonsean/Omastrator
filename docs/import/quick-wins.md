# Quick wins: Inkscape SVG, .svgz, HEIC/AVIF, PSD, Excalidraw, tldraw

Part of the import-open brief (`.omastrator-import-briefs/OPEN.md`). See
`docs/import/sketch.md` and `docs/import/penpot.md` for the two full-format
importers.

## What imports

- **Inkscape SVG layers.** `sodipodi:insensitive="true"` locks the layer.
  `inkscape:label` (the name) and `style="display:none"` (hidden) were
  already read by `SvgSource::label()`/`isHidden()` for every element, not
  just layers, so no change was needed there. A top-level `<g>` already
  became an Omastrator layer regardless of `inkscape:groupmode` (SVG files
  with plain top-level groups get layers too); this is existing, intentional
  behaviour, not something the Inkscape work needed to add.
- **.svgz.** Detected by its gzip magic bytes (`1f 8b`), not the extension,
  so a `.svg` that happens to be gzipped works too. Decompressed with zlib
  before nanosvg ever sees it. `ImageImporter::isVector()` and its SVG name
  filter both learned the `.svgz` suffix so Open/Place route it correctly.
- **HEIC and AVIF**, Place and Open, through libheif (dynamically linked,
  LGPL). Detected by the ISO-BMFF `ftyp` box's brand, not the extension.
  Qt's own `imageformats` plugins on this machine have no HEIF/AVIF plugin
  (confirmed with `QImageReader::supportedImageFormats()`), so this always
  goes through libheif when the library is present at build time; when it
  isn't, the name filters hide the formats and a direct attempt says to
  install `libheif`.
- **PSD for Place/Open**, 8-bit RGB, Grayscale or CMYK, raw or RLE
  (PackBits) compressed. Reads only the merged composite image every PSD
  keeps for compatibility, skipping the colour-mode-data, image-resources
  and layer-and-mask-info sections by their length prefixes. Placed as one
  flattened image, with the warning OPEN.md asked for.
- **Excalidraw** (`.excalidraw`, Open and Place): rectangle (with roundness
  → live corner radius), ellipse, diamond, line, arrow (with arrowheads),
  freedraw (smoothed, not the sketchy wobble), text (font, size, alignment,
  line height), image (decoded from the `files` map's data URL), frames (as
  Omastrator frames, holding whatever elements name them by `frameId`), and
  `groupIds` (nested groups, innermost to outermost). Locked and opacity
  carry over. `strokeStyle` (dashed/dotted) becomes a dash pattern.
- **tldraw (.tldr) wasn't attempted.** OPEN.md lists it explicitly as "a
  stretch goal, for the same kinds of shapes if time allows," last in
  priority order after Sketch, Penpot and the other quick wins. Between
  those, this pass ran out of the time a sixth format needs to be built
  and tested to the same standard as the rest, rather than added as a
  shallower, less-verified afterthought.

## What doesn't

- ZIP-compressed PSD image data (compression method 2/3): refused with a
  plain sentence. Vanishingly rare in practice (Photoshop writes RLE by
  default); revisit if a real file needs it.
- PSD depth other than 8 bits/channel, and colour modes other than RGB/Gray/
  CMYK (Bitmap, Indexed, Multichannel, Duotone, Lab): refused with a plain
  sentence naming the mode. 16-/32-bit and these modes are uncommon for
  flattened placement; a future pass could add 16-bit (take the high byte)
  cheaply if it comes up.
- PSD CMYK→RGB is a naive `255 - min(255, ink + black)` conversion, not
  colour-managed. Good enough for a flattened preview placement, wrong for
  print-accurate colour; noted so nobody is surprised.
- Excalidraw's clipboard JSON (paste, not a file) isn't wired to Ctrl+V.
  `ExcalidrawImporter::parse()`/`canRead()` already accept the clipboard
  variant's shape (`{type: "excalidraw/clipboard", elements, files}`) byte
  for byte the same as a `.excalidraw` file, so the capability exists; only
  the OS-clipboard hookup in the shared UI is missing, which would touch
  more shared code than this pass's "small, additive" scope allows.
- Excalidraw grouping inside a frame isn't tracked: elements with a
  `frameId` become direct children of that frame, dropping their
  `groupIds`. Full fidelity would need a group stack per frame instead of
  one shared with the top level; simpler to note than to build for how
  rarely a frame's contents are also grouped.
- Excalidraw diamonds are always sharp-cornered; Excalidraw can round them
  too (`roundness` applies to diamonds as well as rectangles), but nothing
  in the document model gives a diamond a live shape the way rectangles
  get `LiveRectangle`, so rounding it would mean hand-building a rounded
  diamond path. Not worth it for a rarely-rounded shape.
- Excalidraw text's vertical origin is the top of its bounding box; the
  document model's text origin is the first baseline. A fixed 0.8 em
  ascent approximation closes most of the gap without needing the
  resolved font's real metrics, but it's still an approximation, not
  exact placement.
- Excalidraw's arrow/line "bindings" (a line's end following the shape it
  points at) have no equivalent — only the imported line's fixed geometry
  survives, which is what a designer sees regardless.
- Nested Inkscape layers (`<g inkscape:groupmode="layer">` inside another
  layer) still become an ordinary nested group, like any other nested `<g>`,
  not a second Omastrator layer. Omastrator's layers are a flat, top-level
  concept; making nested Inkscape layers first-class layers would be a
  structural change bigger than "small and well tested" allows for this
  pass, and it isn't what OPEN.md asked for ("top-level" groups only).

## Decisions

- **HEIC/AVIF is one code path**, `ImageImport::readHeicOrAvif()` in
  `src/IO/ImageImporter+Heic.cpp`, driven by libheif for both: modern
  libheif (1.23.4 here) decodes AVIF (AV1) as well as HEIC (HEVC) through
  the same API once built with an AV1 decoder, matching OPEN.md's framing
  of the two formats as one line item.
- **PSD and HEIC/AVIF detection is by magic bytes**, checked before
  `QImageReader` gets the file, inside `ImageImporter::read()` itself,
  rather than as separate importers with their own `read()`/`canRead()`
  functions. Both are *raster* formats that end up exactly where any other
  placed image ends up (one `image`-kind `VectorObject` on a blank
  artboard, or dropped into the current document by Place), so folding them
  into `ImageImporter` reuses `File ▸ Open`/`File ▸ Place`'s existing
  dispatch without adding a single new branch to
  `src/UI/ProjectWorkspace+Files.cpp` beyond passing `&warnings` through
  (see next point).
- **`ImageImporter::read()` grew an optional `QStringList *warnings`
  parameter**, defaulted to `nullptr` so every existing call site still
  compiles unchanged. This is the one shared-file change beyond name
  filters: PSD needs to say "placed as a flattened image" per SHARED.md's
  warning rule, and `ImageImporter::read()` had no way to say anything at
  all before. `ProjectWorkspace::openFile()`/`placeFile()` now pass
  `&warnings` through to `reportLeftOut()`, the same mechanism SVG already
  used.
- **zlib is a `PUBLIC` dependency of `oma_io`** (both the link and the
  `OMASTRATOR_HAVE_ZLIB` compile definition), not `PRIVATE`, so that test
  binaries linking `oma_io` can build zip/gzip fixtures with zlib directly
  and can `#ifdef` around zlib-only tests. Same reasoning for
  `OMASTRATOR_HAVE_LIBHEIF` (definition only, not the libheif link itself,
  since no test needs to call libheif directly).
- **No HEIC/AVIF fixture is checked in.** OPEN.md's test list (Sketch,
  Penpot, an Inkscape SVG, an .svgz, a PSD, an .excalidraw) doesn't ask for
  one, and hand-building a valid HEVC/AV1 bitstream isn't practical without
  a real encoder. The HEIC/AVIF test instead confirms magic-byte detection
  and that a bogus-but-brand-tagged file fails cleanly as a `FileError`
  rather than crashing or hanging; real decoding was verified manually:
  `heif-enc`/`avifenc` (both present on this machine) encoded a 4×4
  solid-colour PNG to `.heic` and `.avif`, and a throwaway program linked
  against `liboma_io.a` read both back through `ImageImporter::read()` and
  confirmed the decoded pixel matched the source colour exactly. Not part
  of the automated suite since it depends on those encoders being
  installed.

- **One rotation formula for every Excalidraw element kind.** Excalidraw
  always gives `x, y, width, height` as the *unrotated* bounding box and
  `angle` as radians about its centre. Rectangles, ellipses and diamonds
  build local geometry in `(0,0)-(w,h)`; lines, arrows and freedraw use the
  local bounding box of their own `points[]` instead (their `x,y` is the
  first point's position, not necessarily the bbox corner). One helper,
  `placementFor(originX, originY, localBounds, angle)`, builds the
  translate→rotate→translate-back `QTransform` either way. Paths bake it
  straight into their geometry with `VectorPath::transformed()`; text and
  images carry it as `VectorObject::transform`, matching how
  `SvgImporter+Content.cpp` already builds its own image transforms
  (`scale * translate * ctm`, left to right, first term applied first).
  Verified by placing a rotated diamond, a multi-point arrow and rotated
  text side by side in `build/omastrator` and comparing the render to what
  Excalidraw itself would show for the same numbers.
- **Excalidraw's own current and legacy font names are used directly**
  (`Excalifont`, `Nunito`, `Comic Shanns Mono`, `Virgil`, `Helvetica`,
  `Cascadia Code`, …), rather than substituting an installed look-alike.
  The app already has a general "missing fonts" flow
  (`EditorSession::missingFonts()`, surfaced after every Open) that flags
  whichever of these aren't installed and points at Type ▸ Find/Replace
  Font…, so the importer doesn't need its own font-substitution logic —
  confirmed working end to end in the manual `build/omastrator` smoke test
  below, which showed "Missing font: Excalifont" exactly as expected.

## Report

Manually verified in `build/omastrator` (offscreen, `OMASTRATOR_SNAPSHOT`):
a rounded rectangle, an ellipse, a rotated diamond, a multi-point arrow with
a triangle head, and text all opened, rendered and positioned correctly
from a hand-written `.excalidraw` file, with the Layers panel naming each
one and the missing-font banner catching the unavailable font by name.

All automated tests pass: `ZipReaderTests` (6), `SvgImportTests` (28, up
from 26), `ImageImporterTests` (11, up from 3), `ExcalidrawImportTests`
(14, new), and `ProjectWorkspaceTests` (14, up from 13, covering the new
Open/Place wiring) — run with `QT_QPA_PLATFORM=offscreen`.

(Filled in as each piece lands; see the end of this document for the final
state.)
