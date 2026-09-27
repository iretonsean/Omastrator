# Handoff: text flow (P2-3 type on a path, P2-4 wrap and threads, P2-5 hyphenation)

Paused on 2026-09-27 to save usage budget, then finished the same day: the
design below shipped as written, merged with main's artboards (.omai v5),
width tool and phase-4 work. See the P2-3/4/5 note in QOL-RESEARCH.md and the
README's Type on a Path/wrap/threads/hyphenation entry for the final summary.

## Done

- [x] Merged `origin/main` (it was already up to date at 3f9a874).
- [x] Read VISION, AGENTS, HUMOR, QOL-RESEARCH (P2-3/4/5), and the text code:
      `TextLayout`, `TextContent`, `DocumentCodec`, `InlineTextEditor`,
      `EditorCanvas+Text`/`+Paint`/`+Keys`/`+Dispatch`, `SvgExporter::writeText`,
      `SvgImport::readText`, `EditorSession` (edit, notify, interaction).
- [x] Checked Qt's soft hyphens with a scratch program. `QTextLayout` breaks at
      U+00AD, draws a hyphen glyph at the line end (it shows up in
      `QTextLine::glyphRuns()`), and draws nothing mid-line. **Soft hyphens
      already work in area type through QTextLayout.** P2-5 part 1 only needs a
      test, plus SVG export: strip U+00AD and append `-` on a line that ends at
      one.
- [x] Chose the hyphenation patterns: hyph-utf8 `hyph-en-us` (Gerard D.C.
      Kuiken; Knuth/Liang patterns). The licence permits copying and
      modification if the copyright notice and licence notice are kept. It's
      4938 patterns plus 14 exceptions. Download from
      `https://raw.githubusercontent.com/hyphenation/tex-hyphen/master/hyph-utf8/tex/generic/hyph-utf8/patterns/{txt/hyph-en-us.pat.txt,txt/hyph-en-us.hyp.txt,tex/hyph-en-us.tex}`
      (the .tex file has the licence header). On this machine, Arch has
      `hyphen` (libhyphen) installed but not `hyphen-en`, and there's no
      `/usr/share/hyphen`, so vendor the patterns and write Liang's algorithm
      in-tree (about 100 lines). Optionally, also read
      `/usr/share/hyphen/hyph_<lang>.dic` at runtime when present.
- [x] Checked keys: plain Shift+T is free. Only the plain `T` (Type tool) is
      bound. Shift+M is Shape Builder.

## Design (decided)

### Model (additive; codec keys are optional, so no migration is needed)

- `ParagraphFormat`: `bool hyphenate = false`, and `int hyphenMinWord = 6`,
  `hyphenMinBefore = 2`, `hyphenMinAfter = 3`. Write and read them in
  `writeParagraph`/`readParagraph`, so paragraph styles carry them too.
- `TextContent::onPath`: `std::optional<TextPath>` with
  `{ VectorPath path /*text-local*/; double start /*0..1 along the original path*/; bool flipped; }`.
  Flip reverses the path (`toReversed`). The effective start is `1 - start`
  when flipped, so flipping keeps the bracket in place. Codec key: `"onPath"`.
- `TextContent::threadNext`: a `QUuid`, where null means none. Codec key:
  `"threadNext"`.
- `VectorObject::textWrap`: `std::optional<double>`, the offset in pt. Codec key:
  `"textWrap"`.
- Derived, never saved: `TextContent::flow` (`TextFlow`), with
  `std::shared_ptr<const TextContent> story` (followers get the head's content),
  `QUuid head`, `std::vector<TextFrame> frames` (`{QSizeF size; std::vector<QRectF> exclusions /*frame-local*/; QTransform transform /*frame→document*/}`)
  and `int frame`. Give it a custom `operator==` that compares the story by
  value.
- `VectorDocument::reflowText()` fills `flow`. It returns early when no object
  has a `textWrap` and no text has a `threadNext`, and clears any stale flows.
  Wrap objects count only when they're above the text in paint order. Paint
  order is the index in `objects`, because subtrees are contiguous. The
  exclusion is the wrap object's bounds grown by the offset, mapped into the
  box's local coordinates. Threads follow `threadNext` through area-type boxes
  only, with a cycle guard.
- Call `reflowText()` in `EditorSession::pruneSelection()` (it runs before
  `history.end`), in `notify(true)` (for previews), and at the end of
  `DocumentCodec::decode`.
- `remove()` clears any `threadNext` that points at a removed id.
  `copySubtree`/paste remaps it, or drops it when the target isn't copied too.
- Bump `DocumentCodec::version` to 4 with a one-line comment. Other agents bump
  it too; merge the comment lines. Decoding is key-based, so the combined
  version still reads.

### TextLayout

- Build from `text.flow.story ? *story : text`, and keep `m_flow`.
- `Line` gains `frame`, `available` (the segment width, used by `shift()` and
  the justify-centre/right offset in place of the area width), `pathOffset`
  and `hyphenated`.
- Frames/exclusions mode, used only when `flow.frames` isn't empty. For each
  row, guess the baseline from the next character (first line in a frame: y +
  ascent; later lines: + leading). Take the free intervals of
  `[leftIndent, width-rightIndent]` minus the exclusion x-ranges that meet the
  band, and skip intervals narrower than `2×size`. Make a `QTextLine` per
  interval, calling `setLineWidth`/`setPosition`. A line can be re-set before
  the next is created. When a row passes the frame's height, move to the next
  frame. Past the last frame the lines are hidden and `m_overflows` is set. The
  existing code path must stay exactly as it is when there are no frames, so
  current tests keep passing.
- `outline()`/`fills()` draw only the lines where `line.frame == flow.frame`.
- Path mode: lay each paragraph out as a NoWrap line, placed one after another
  along the path (`pathOffset`). For each glyph, `mid = x + advance/2`, where
  advance comes from `QRawFont::advancesForGlyphIndexes × m_horizontal`. Use an
  arc-length walk: `t = percentAtLength(startLen + pathOffset + mid)`,
  `P = pointAtPercent(t)`, and the tangent from P(s±δ). Place each glyph with
  `translate(-mid, -baseline) * QTransform(ux, uy, -uy, ux, P.x, P.y)`.
  Closed paths wrap once. Glyphs past the end are hidden and set overflow.
  Underline and strike aren't drawn on a path (a deviation).
- New `QPointF warp(QPointF local, int line) const`. Path mode uses the path
  mapping above. For threads, a line in frame k maps by
  `frames[k].transform * frames[flow.frame].transform.inverted()`. Otherwise
  it's the identity.
- `positionAt(local)` for path text picks the nearest warped caret point. For
  threads, it un-warps into each frame and picks the frame that contains the
  point. Within a row, the nearest segment wins.
- Hyphenation: when a paragraph's `hyphenate` is on, build the display string
  with U+00AD inserted at the pattern breaks, and keep `toDisplay` and
  `fromDisplay` index maps. Map ranges as `[toDisplay[a], toDisplay[b])`. Lines
  report their start and length in the original indices, and `rawX` and
  `glyphRuns` convert. Qt then draws the hyphen.
- `TextLayout::source()` returns the text being laid out, for the SVG exporter.

### Editor and canvas

- `InlineTextEditor`: map the caret, the selection band and the preedit through
  `layout().warp()`. On a path, draw the selection band as a polygon sampled
  along the path.
- A double-click or Type-tool click on a follower edits the head
  (`flow.head`). `textBox()` takes in every frame.
- New `Tool::typeOnPath` (Shift+T, title "Type on a Path"). Add it to
  `allTools`, the rawValue table in `EditorSession.cpp`, the `ContentView` tool
  group next to Type, `ToolIcons`, the cursors, the status text, and
  `KeyboardShortcuts::toolKeys` (the array size goes from 16 to 17, key "t"
  with modifier 8). Clicking a path makes a text object with `onPath` = that
  path, `transform` = identity, the path's fill, and removes the path, as
  Illustrator converts it. That's one undo step named "Type on a Path". Then
  it opens the editor.
- Selected path text shows a start bracket at the effective start point.
  Dragging it slides `start` (to the nearest point on the path) in an
  interaction named "Move Type on a Path". Dragging across the path, or Type ▸
  Type on a Path ▸ Flip, flips it (undo step "Flip Type on a Path").
- Threads: the existing red ⊞ out port in `drawOverlay` stays on the last box of
  a chain. Every selected area box shows an in port (top left) and an out port
  (bottom right). A click on an out port arms "link" mode. Then a click on
  another area box links it; a click on empty canvas makes a box the same
  size, and a drag makes one of the dragged size. That's one undo step, "Thread
  Text". Linking into a box that already has text appends that text as a new
  paragraph. Type ▸ Threaded Text ▸ Remove Threading splits the story so each
  box keeps what it shows (use `TextContent::replace` to cut the ranges).
  Draw thread lines between the ports of linked, selected boxes.
- Object ▸ Text Wrap ▸ Make / Release / Text Wrap Options… (the offset).
  Put the offset field in Properties when a wrap object is selected. Each is
  one undo step via `EditorSession::setTextWrap(std::optional<double>)`.
- ParagraphSection: a "Hyphenate" checkbox, with its settings behind a
  disclosure (words longer than, after first, before last).

### IO

- SvgExporter `writeText`: use `layout.source()` for the strings and formats.
  Skip lines that belong to another frame or are hidden (`continue`, not
  `break`). Strip U+00AD, append `-` when `line.hyphenated`, and keep the `dx`
  kern indices in step. For path text, write
  `<defs><path id=… d=…/></defs>` (reversed when flipped, and doubled when a
  closed path wraps), then
  `<text><textPath href="#…" startOffset="N%">…runs…</textPath></text>`.
- SvgImporter: `readText` already collects `textPath` children. Record the
  href target and `startOffset`, then parse the target's `d` through nanosvg
  (add `SvgImport::parsePathData(const QString &d)` in `SvgImporter.cpp`, which
  wraps it in `<svg><path d=…/></svg>`). Set `onPath` on the content, in the
  text's local coordinates.
- Create Outlines works through `fills()` without changes. Check that
  `convertTextToPaths` uses `fills()`.

## Done (finished 2026-09-27)

- [x] Model fields, codec keys (still additive, no bump of their own),
      `reflowText()`, and the remove/copy id fixes.
- [x] Frames and exclusions in the TextLayout rows; the path mode; `warp`;
      `positionAt`.
- [x] Hyphenator (`src/Document/Hyphenator.{h,cpp}`, Liang). Patterns vendored
      in `third_party/hyph-utf8/` with the licence kept, compiled in via
      `cmake/HyphenPatterns.h.in`. Provenance line in AGENTS.md.
- [x] Editor warp; Type on a Path tool (Shift+T — free even after main's
      Width tool took Shift+W and Artboard took Shift+O); bracket and flip;
      thread ports and link mode; wrap menu/Properties; Paragraph Hyphenate
      toggle.
- [x] SVG export/import (`<textPath>`, hyphens, frames).
- [x] Tests: `tests/Document/TextFlowTests.cpp` and `tests/IO/TextFlowIOTests.cpp`,
      per the list below this once had; both green.
- [x] Marked P2-3/4/5 done in QOL-RESEARCH.md and in the README.
- [x] Merged `origin/main` (artboards/.omai v5, width tool + opacity masks,
      phase-4 sites and Desktop Look); resolved the tool-list/rail/shortcut
      conflicts so Type on a Path, Width and Artboard all keep their keys and
      slots. `CHECK OK` at 100% (92 suites), `SWEEP OK` against origin/main.
