# Importing PDF, Illustrator (.ai) and EPS/PostScript

Branch `feat/import-pdf`. The rules are in [SHARED.md](../../../.omastrator-import-briefs/SHARED.md)
and [PDF.md](../../../.omastrator-import-briefs/PDF.md) (outside the repo, in
the three-agent brief). This doc is PDF.md's own required write-up: the
decisions made along the way, then what imports, what doesn't, and what
couldn't be tested without real files.

## What's here

- `src/IO/PdfLexer.{h,cpp}`, `PdfObject.{h,cpp}` — the token grammar and
  object model (numbers, strings, names, arrays, dictionaries, indirect
  references) PDF's file structure and content streams share.
- `src/IO/PdfDocument.{h,cpp}` — the file structure: classic and
  cross-reference-stream xrefs, object streams, incremental updates
  (following `/Prev`, newest wins), a scan-for-"obj" repair, encryption
  refused.
- `src/IO/PdfFilters.{h,cpp}` — FlateDecode, LZWDecode, ASCIIHex, ASCII85,
  RunLength, PNG and TIFF predictors.
- `src/IO/PdfFunction.{h,cpp}` — function types 0 (sampled), 2 (exponential),
  3 (stitching) and 4 (a small PostScript calculator).
- `src/IO/PdfColorSpace.{h,cpp}` — DeviceGray/RGB/CMYK, ICCBased, Indexed,
  Separation/DeviceN, Lab.
- `src/IO/PdfEncoding.{h,cpp}` — WinAnsi/MacRoman/Standard base encodings, a
  glyph-name-to-Unicode table, ToUnicode CMap parsing, and font-name to
  installed-family matching.
- `src/IO/PdfContent{.h,.cpp,+Text.cpp,+Image.cpp,+Shading.cpp}` — the
  content-stream interpreter that builds live `VectorDocument` paths, text
  and images.
- `src/IO/PdfImporter.{h,cpp}` — ties it together: pages become artboards.
- `src/IO/AiImporter.{h,cpp}` — Illustrator `.ai`.
- `src/IO/EpsImporter.{h,cpp}` — EPS and plain PostScript, via Ghostscript.
- `src/IO/VectorFileImporter.{h,cpp}` — the one dispatch point Open, Place,
  drag-and-drop and the agent's `place` tool all go through.
- Tests: `tests/IO/PdfImporterTests.cpp`, `PdfRoundTripTests.cpp`,
  `AiImporterTests.cpp`, `EpsImporterTests.cpp`, `PdfFixtures.h` (the
  hand-written PDF writer the tests build fixtures with).

## Decisions

- **FlateDecode without linking zlib.** `oma_io` had no compression
  dependency before this branch. Rather than add `find_package(ZLIB)`,
  `PdfFilters::inflate` prepends a synthetic 4-byte big-endian size hint to
  the raw FlateDecode bytes and calls Qt's own `qUncompress`, which expects
  exactly that framing around an ordinary zlib stream. A wrong hint only
  costs `qUncompress` a retry with a doubled buffer (verified empirically:
  hints from 1 to 100,000 all inflate the same real-world FlateDecode data
  correctly). This keeps the PDF reader's only new "dependency" being Qt
  itself, which already links zlib internally.
- **`Object::toArray()`/`toDict()` return by value, not `const&`.** They
  first returned references into the `Object`'s own `shared_ptr`-held
  storage, which dangles the moment they're called on a temporary — exactly
  the common `document.resolve(x).toArray()` pattern used throughout this
  code. `QList`/`QHash` are reference-counted, so returning by value costs
  nothing on the common path and removes the whole class of bug (caught by
  GCC's `-Wdangling-reference` during development, before it could reach a
  real file).
- **Text's local space needed an explicit sign flip.** `TextContent`'s
  glyph space is Qt's own (y-down, ascenders negative), not PDF's own text
  space (y-up); feeding Tm/CTM (built from PDF's y-up math) straight to
  `VectorObject::transform` flipped text twice over — confirmed with an
  actual app screenshot ("Hello, World!" read backwards and upside down)
  before the fix, and right side up after. `PdfContent+Text.cpp` negates y
  once, first, to correct for it.
- **`QPainterPath::isEmpty()` doesn't mean "no subpath started".** It's Qt's
  own documented behaviour: a path holding only a `moveTo` still reports
  `isEmpty() == true`. The content interpreter used to ask `isEmpty()` to
  decide whether `l`/`c`/`v`/`y` needed a fallback `moveTo` of their own,
  which meant every line or curve after a bare `m` (with no prior fill/
  stroke) silently became another `moveTo`, and the whole subpath was lost.
  A hand-written fixture using `re` never hit this; a Qt-exported PDF's
  `m l l l l h f*` rectangle did, caught by the round-trip test. Fixed with
  an explicit `m_hasOpenSubpath` flag, saved and restored around Form
  XObject recursion the same way the path itself is.
- **An SMask's gray value is its own alpha, not a channel to read off it.**
  `decodeImageObject` builds a soft mask through the same DeviceGray
  decoding path as any other image, which produces an opaque (alpha 255)
  `QImage` — the mask's actual value lives in the RGB channels, not alpha.
  Checking `QImage::hasAlphaChannel()` to decide between `.alpha()` and
  `.red()` always picked `.alpha()`, so every soft mask came out fully
  opaque. Fixed by tracking whether the alpha source was a luminosity
  `/SMask` or a genuine-alpha stencil `/Mask` explicitly, rather than
  inferring it from the decoded image's format.
- **Radial gradients are approximated.** Omastrator's `Paint` model is one
  circle growing from a center to a radius; a PDF radial shading's two
  distinct circles (`r0`, `r1`, possibly non-concentric) are approximated by
  the ending circle alone. This matches the very common `r0 == 0`,
  concentric case exactly, and is a reasonable single circle for anything
  else.
- **Tiling patterns get a flat fill, not a warning-only skip.** PDF.md
  allows either; a flat fill (the pattern's underlying/average color where
  known, gray otherwise) keeps the object visible and selectable rather
  than invisible, which seemed more useful given "lift, don't trace."
- **Multi-page layout is left-to-right, not stacked.** Nothing in the brief
  specifies a direction; left-to-right with a 40pt gap was the simplest
  choice and mirrors how Illustrator's own default new-artboard layout
  reads.
- **OCG layers are shared across pages, not duplicated per page.**
  Omastrator's layers already span every artboard (there's no per-artboard
  layer list), so a `BDC /OC` block is mapped once per distinct Optional
  Content Group (keyed by its object number) to one top-level layer, reused
  if content in a later page references the same OCG. Content outside any
  `/OC` block stays in that page's own "Page N" layer.
- **Ghostscript is a program, never a library**, consistent with the
  licence rule (no Poppler/MuPDF linked in) — run via `QProcess`, located
  with `QStandardPaths::findExecutable`, overridable with `OMASTRATOR_GS`
  the way `OMASTRATOR_RCLONE` works. Missing Ghostscript throws
  `FileError("Ghostscript isn't installed, so this file can't be converted.
  Install it with: omarchy pkg add ghostscript")`, matching SHARED.md's
  external-program rule exactly (the general codebase convention elsewhere
  is `sudo pacman -S <pkg>` for background tools — SHARED.md's explicit
  `omarchy pkg add <pkg>` wording was followed here since it's what this
  specific brief asked for).
- **Wiring goes through one new dispatcher, `VectorFileImporter`, not three
  copies of a suffix switch.** `ImageImporter::isVector`/`nameFilters()` now
  recognise `.pdf`/`.ai`/`.eps`/`.ps` (they already gated Open, Place,
  drag-and-drop and the agent's tools); the three call sites that used to
  hardcode `SvgImporter::read` now call `VectorFileImporter::read` instead,
  a one-line swap each. File > Place's "multi-page PDF: place the first
  page, warn about the rest" rule lives in
  `VectorFileImporter::readFirstArtboard`, built on the existing
  `VectorDocument::artboardDocument(0)`, and applies uniformly (SVG never
  produces more than one artboard, so the check is a no-op for it).
- **Round-trip text comes back as filled paths, not a live text object —
  and that's `DocumentExporter`'s doing, not this importer's.**
  `VectorRenderer::draw` (shared by every export format: PDF, PNG, JPEG,
  the canvas itself) draws text by filling `TextContent::outline()`, never
  by calling `QPainter::drawText`. A PDF `DocumentExporter::writePdf`
  produces therefore never contains `Tf`/`Tj` for text at all, in this
  environment or any other — confirmed by decompressing an actual exported
  content stream and finding a single `f` fill operator with fourteen
  contours for the string "Round Trip", not a single text-showing operator.
  `PdfRoundTripTests.cpp` checks what actually round-trips (the rectangle's
  fill color, and the text's glyph outlines surviving as enough contours),
  and says why in a comment, rather than asserting something the export
  pipeline was never going to produce.

## What imports

- Paths: `m l c v y h re`, filled (`f`/`F`/`f*`) and stroked (`S`/`s`, plus
  `B`/`B*`/`b`/`b*`), with the correct fill rule, line width, cap, join,
  miter limit and dash array (scaled by the CTM).
- Clipping (`W`/`W*`) as nested clip groups, the way `SvgImporter` already
  represents clips.
- Color: DeviceGray/RGB/CMYK (CMYK converted plainly, noted once per
  import), ICCBased (via `/N` or `/Alternate`), Indexed, Separation and
  DeviceN (via their tint-transform function, types 0/2/3/4), Lab.
- Shadings: axial and radial (`sh`, and shading patterns via `scn`), sampled
  into up to 16 gradient stops.
- Form XObjects as (clip) groups, with their own `/Matrix` and `/BBox`
  clip, and their own `/Resources` (falling back to the caller's).
- Image XObjects and inline images (`BI`/`ID`/`EI`): raw samples at any bit
  depth, DCTDecode (JPEG) images, `/SMask` and stencil `/Mask` alpha,
  `/ImageMask` stencils painted with the current fill color.
- Text: `BT`/`ET` and the full text-state operator set, rebuilt as one
  live `TextContent` per baseline (`PDF.md`'s "merge runs on one baseline
  into point-type lines"), with per-run family/style/size/color via
  `TextContent::runs`. Glyphs map to Unicode through ToUnicode first, then
  the font's `/Encoding` (base table plus `/Differences`), then the
  standard encoding tables. The font family and style come from
  `/BaseFont` (subset prefix stripped, `-Bold`/`Italic`/`Oblique` suffixes
  parsed) matched against `QFontDatabase::families()`; an unmatched family
  is kept as-is (so the existing Type ▸ Find/Replace Font… can fix it) and
  flagged once per family. Render mode 3 (invisible, often an OCR layer)
  imports as regular visible text, noted once; render mode 7 (text used
  only to clip) is left out, noted once.
- Optional content: `/OCProperties`/`BDC /OC … EMC` become named layers,
  shared across pages by OCG identity; an OCG listed in `/D /OFF` imports
  hidden.
- Structure: each page becomes an artboard (from `/CropBox`, falling back
  to `/MediaBox`), laid out left to right; `/Rotate` is honoured (verified
  against Ghostscript's own rendering of a 90°-rotated asymmetric fixture,
  pixel region by pixel region — 180° and 270° are implemented the same
  way but weren't independently pixel-checked).
- The classic xref table, cross-reference streams, object streams, and
  incremental updates (`/Prev`, newest offset wins); a damaged xref is
  repaired by scanning for `N G obj`.
- Illustrator `.ai`: a modern file (`%PDF` header) is read as PDF directly;
  a legacy PostScript-based one (`%!PS-Adobe` header) goes through the EPS
  path. A file saved without PDF content (Illustrator's own placeholder —
  detected as a PDF whose pages hold no path, text or image at all) is
  refused, naming "Create PDF Compatible File".
- EPS and plain PostScript, converted with Ghostscript
  (`-dSAFER -sDEVICE=pdfwrite -dEPSCrop`) and read as the PDF it produces. A
  DOS EPS binary header's PostScript section is cut out first. **Ghostscript
  kept text as real text for every fixture tried** (confirmed: the
  converted PDF's font is a synthetic subset name like `XVGOTT+Anonymous`
  rather than the original PostScript font name, but the string content and
  a `Tf`/`Tj` pair are genuinely there) — so EPS text imports live, the
  same as PDF text, just always flagged as a substitute font since the
  subset name never matches an installed family.

## What's left out, with a warning

- Mesh shadings (types 4–7).
- Tiling patterns (filled with a flat color instead of a warning-only
  skip — a deliberate choice, see Decisions).
- A soft mask set through an ExtGState's `/SMask` (a full transparency
  group), as opposed to an image's own `/SMask`, which does work.
- Color-key masking (`/Mask` as an array of ranges, not a stencil image).
- CCITTFaxDecode (fax-compressed scanned images) and any `JPXDecode`
  (JPEG2000) image this Qt build's plugins can't decode.
- Any filter this branch doesn't implement, named in the warning.
- A composite font with a CMap other than Identity-H/V (read as if it were
  Identity anyway, which is usually close enough for the byte-width but can
  give wrong glyph IDs).

## Left out entirely, out of scope

- **Type 3 fonts** (glyphs defined as little content-stream programs) have
  no family/style equivalent in this codebase's font model and aren't
  special-cased; a Type 3 font's text still decodes to the right Unicode
  string through ToUnicode/Encoding, but the family lookup will always miss
  and get flagged as a substitute, same as any other unmatched font.
- **Vertical writing** (`/WMode 1`, `Identity-V`): read as if it were
  horizontal.
- Illustrator's own private data (`AIPrivateData`) — explicitly out of
  scope per PDF.md.

## What couldn't be tested without real files

- A real Illustrator- or InDesign-exported PDF or native `.ai` file, an
  Adobe-exported EPS with an actual embedded PostScript font, or a
  real-world scanned/faxed PDF. Everything above was verified against
  hand-written fixtures (`tests/IO/PdfFixtures.h`), Omastrator's own
  export, and Ghostscript's real conversion and rendering (`gs` is
  installed on this machine, so `EpsImporterTests`' fake-`gs` path and a
  direct `EpsImporter::read` against real Ghostscript output were both
  exercised, though only the fake is in the committed test suite, matching
  PDF.md's "an EPS through a fake gs").
- Non-Identity CMaps, right-to-left/vertical text, and CJK fonts more
  generally — no fixture exercises these.
- A truly huge or pathological PDF (deeply nested Form XObjects beyond the
  12-level guard, a multi-megabyte xref, thousands of pages) for
  performance; nothing here was measured against one.
