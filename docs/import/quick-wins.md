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
- **Excalidraw** (`.excalidraw` and the clipboard JSON): see the "Decisions"
  section below once implemented.

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

## Report

(Filled in as each piece lands; see the end of this document for the final
state.)
