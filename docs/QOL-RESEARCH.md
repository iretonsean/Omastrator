# Quality-of-life research: Illustrator, Figma and Paper

Omastrator looks right at first glance. Where it falls short is quality of
life: there are no right-click menus, the type controls stop at family, size,
bold, italic and three alignments, and the small precision features designers
reach for every minute aren't there. This document covers what Adobe
Illustrator, Figma and paper.design ship in those areas, measures Omastrator
against them, and ends with a phased build plan.

It's based on official documentation: the Adobe Illustrator user guide, the
Figma Help Center and paper.design's docs. Where a vendor doesn't document
something, the text says so rather than guessing. Sources are linked at the end
of each section. Adobe pages were read through the Internet Archive, because
helpx.adobe.com refuses scripted requests.

Contents:

1. [What Omastrator has today](#1-what-omastrator-has-today)
2. [Context menus](#2-context-menus)
3. [Typography](#3-typography)
4. [Detail-oriented and QoL features](#4-detail-oriented-and-qol-features)
5. [Paper: what it does differently](#5-paper-what-it-does-differently)
6. [UI polish patterns](#6-ui-polish-patterns)
7. [UI polish for Omastrator's existing panels](#7-ui-polish-for-omastrators-existing-panels)
8. [Gap analysis and prioritized build plan](#8-gap-analysis-and-prioritized-build-plan)

---

## 1. What Omastrator has today

Read from the source (`src/`) and the screenshots in `docs/screenshots/` as of
this commit.

| Area | Today | Where |
|---|---|---|
| Context menus | **None on the canvas or the Layers panel.** The only right-click menus are on swatches and swatch-group titles. | `SwatchesPanel.cpp` |
| Text model | `TextContent`: one style per object (family, size, `bold`, `italic`, 3 alignments, `leading` as a multiple, `tracking` in absolute pt). Point type only. It renders through `QPainterPath::addText`, one line at a time. | `Document/VectorDocument.h/.cpp` |
| Type UI | The tool-options bar appears only while the Type tool is active: family combo, size, B, I, and left/center/right. **`tracking` and `leading` exist in the model and round-trip through SVG, but nothing in the UI edits them.** With a text object selected under the Selection tool, the Properties panel shows no type section. | `UI/TypeControls.cpp`, `UI/ToolHeaders.cpp` |
| Font style | Only a `bold` flag. Light, Medium, Semibold, Black, condensed and other faces can't be chosen. | `TextContent::font()` |
| Properties panel | Transform (X/Y/W/H/rotation), Artboard (W/H), Appearance (one fill, one stroke, blend, opacity), Stroke (weight, cap, corner, dashes), Align (six align buttons, two distribute buttons, Selection/Artboard target) and Pathfinder (four icon buttons). | `UI/PropertiesPanel*.cpp` |
| Number fields | Return applies, arrow keys step by 1, Shift steps by 10. **No math, no units, no scrubbing.** | `UI/NumberField.cpp` |
| Selection and snapping | Smart guides (edges, centres, equal spacing), grid with snap-to-grid, and Outline mode (Ctrl+Y). No rulers, no guides, no Alt-hover distances. | `Canvas/SmartGuides.*`, `EditorSession` view flags |
| Nudge | Fixed at 1 pt, or 10 pt with Shift. | `Canvas/EditorCanvas+Keys.cpp` |
| Artboards | One per document (`VectorDocument::size`). | `VectorDocument.h` |
| Stroke | At the time of this survey, `StrokeAlignment` was declared but unused, with no arrowheads. Now P1-8: alignment, arrowheads and dashes aligned to corners. No width profiles. | `Document/Paint.h`, `Document/StrokeGeometry.*` |
| Edit menu | Paste and Paste in Place are there; Paste in Front and Paste in Back are not. **Duplicate is bound to Ctrl+J, which is Illustrator's Join.** No Transform Again (Ctrl+D). | `UI/Menus.cpp` |
| Menus | File, Edit, Object, Type (Create Outlines only), View, Window, Help. **There is no Select menu**, so no Select Same, Inverse or Next Object Above. | `UI/Menus.cpp` |
| Undo | Named undo steps. No History panel. | `DocumentHistory` |
| Export | PNG, JPEG, SVG and PDF of the whole document. No per-object or per-selection export, and no scale presets. | `UI/ExportSheet.cpp` |
| Command palette | None at the time of this survey; now Ctrl+K (P1-14). The Keyboard Shortcuts sheet can remap every action. | `UI/KeyboardShortcuts*`, `UI/CommandPalette*` |

It already does well at: theme integration, named undo steps with
begin/preview/commit interactions, smart guides, remappable shortcuts, clear
tool hints in the header bar and status bar, and the AI and Live features.

---

## 2. Context menus

### Illustrator

Adobe's help says a context menu shows "commands relevant to the active tool,
selection or panel". Only the artboard menu is documented item by item.
Commands that other pages say are reachable by right-click are marked *(doc)*;
the rest is how Illustrator is commonly laid out and should be checked against
the running app.

- **Blank canvas, nothing selected:** Undo/Redo, Paste, Zoom In/Out, Show/Hide
  Rulers and Grid, Hide/Lock/Release Guides, and a Select submenu.
- **Object or path:**
  - Undo/Redo.
  - Isolate Selected Group or Path *(doc)*.
  - Simplify…, Join (Ctrl+J), Average… (Alt+Ctrl+J).
  - Make Clipping Mask (Ctrl+7), Make Compound Path (Ctrl+8), Make Guides (Ctrl+5).
  - **Transform ▸** Transform Again (Ctrl+D), Move… (Shift+Ctrl+M), Rotate…,
    Reflect…, Scale…, Shear…, Transform Each… (Alt+Shift+Ctrl+D), Reset
    Bounding Box.
  - **Arrange ▸** *(doc)* Bring to Front (Shift+Ctrl+]), Bring Forward
    (Ctrl+]), Send Backward (Ctrl+[), Send to Back (Shift+Ctrl+[).
  - **Select ▸** First, Next or Last Object Above or Below.
  - Collect for Export ▸ *(doc)*, Export Selection…, Make Pixel Perfect *(doc)*.
- **Group:** adds Ungroup (Shift+Ctrl+G), Ungroup All and Isolate Selected
  Group *(doc)*.
- **Multiple objects:** adds Group (Ctrl+G), and Objects on Path ▸ Attach *(doc)*.
- **Locked object:** Unlock ▸ *object name* *(doc)*.
- **In isolation mode:** Exit Isolation Mode *(doc)*.
- **Text selected with the Selection tool:** Create Outlines (Shift+Ctrl+O)
  *(doc)*, plus the object items above.
- **Text being edited:** Font, Recent Fonts, Size, Glyphs, Change Case, Insert
  Special / Whitespace / Break Character (Forced Line Break *(doc)*), Show Hidden
  Characters, Snap to Glyph *(doc)*, and Cut/Copy/Paste.
- **Image:** Crop Image, Embed/Unembed, Image Trace.
- **Artboard (with the Artboard tool, Shift+O), fully documented:** Cut, Copy,
  Paste; Undo, Redo; New Artboard, Duplicate Artboards, Rename, Delete
  Artboards, Delete Empty Artboards; Select All Artboards; Lock Artboards ▸
  (Selected / All Except Selected / All); Rearrange; Fit to Artwork Bounds;
  Switch Orientation; Artboard Options; Export.
- **Layers panel:**
  - Row modifier clicks:
    - Alt-click an eye: show or hide every other layer.
    - Ctrl-click an eye: switch that layer between Outline and Preview.
    - Alt-click a lock: lock or unlock every other layer.
    - Double-click a row: Layer Options (name, colour, template, show,
      preview, lock, print, dim images).
  - Panel menu: New Layer or Sublayer, Duplicate, Delete, Options for *layer*,
    Enter or Exit Isolation Mode, Locate Object, Merge Selected, Flatten
    Artwork, Collect in New Layer, Release to Layers, Reverse Order, Hide
    Others, Outline Others, Lock Others.
  - The panel's footer also has Make/Release Clipping Mask, Locate Object and
    Collect for Export.

### Figma

Figma doesn't publish its full menus either. The items below are confirmed in
help articles unless marked *(unverified)*. Every item shows its shortcut
right-aligned.

- **Canvas, nothing selected:** Paste here (at the pointer), Show/Hide UI,
  and *(unverified)* Show/Hide comments, Plugins, Widgets.
- **Object** (UI3 order, *(unverified)* where not linked):
  - Copy; Paste here; Paste to replace (Ctrl+Shift+R).
  - **Copy/Paste as ▸**
    - Copy as text / code (CSS, iOS, Android) / SVG / PNG (Ctrl+Shift+C).
    - Copy link.
    - Copy properties (Ctrl+Alt+C) and Paste properties (Ctrl+Alt+V).
  - **Select layer ▸** A hover list of every layer stacked under the pointer.
  - **Select matching layers** (Ctrl+Alt+A): same name, same parent names,
    same position in the hierarchy. Text layers match by text style.
  - Bring to front / forward / backward / back.
  - Group (Ctrl+G), Frame selection (Ctrl+Alt+G), Ungroup (Ctrl+Shift+G).
  - Flatten (Ctrl+E), Outline stroke (Ctrl+Alt+O), Use as mask (Ctrl+Alt+M).
  - Add auto layout (Shift+A).
  - Create component (Ctrl+Alt+K); Reset or Detach instance (Ctrl+Alt+B).
  - Show/Hide (Ctrl+Shift+H), Lock/Unlock (Ctrl+Shift+L).
  - Flip horizontal (Shift+H), Flip vertical (Shift+V).
  - Rename (Ctrl+R).
  - Set as thumbnail (frames only).
- **Image:** Replace image. Cropping is not in the menu; it's a double-click or
  the fill's Crop mode.
- **Layers panel rows:**
  - The same menu as the object.
  - Selection keys: Shift-click selects a range, Ctrl-click toggles one row.
    Enter selects children, Shift+Enter the parent, Tab and Shift+Tab move
    between siblings.
  - Double-click to rename.
  - The eye and lock show on hover.

### Paper

Paper refers to a paste context menu without documenting it. **⌘-right-click
lists every layer under the pointer**, the same idea as Figma's Select layer ▸.

### Takeaways for Omastrator

1. **One menu builder, reused everywhere.** The canvas, Layers panel rows and
   (later) artboards should share the named `QAction`s that `Menus` already
   owns. Shortcuts then show automatically, and remaps in the Keyboard
   Shortcuts sheet carry over.
2. **Contextual, not exhaustive.** Show only what applies to the hit object
   and the selection: text gets Create Outlines, groups get Ungroup and
   Isolate, locked objects get Unlock ▸ *name*, images get Image Trace.
   Illustrator's rule is that the menu reflects the selection; don't list
   disabled items that could never apply.
3. **"Select layer ▸" (Figma) / "Select ▸ Next Object Below" (Illustrator)**
   is the right-click feature people miss most for overlapping art. It's cheap
   to build: `hitTest` already exists; it needs an "all hits" version.
4. **Copy/Paste properties** (Figma) is a big quality-of-life win that fits
   `EditorSession::pickStyle` almost as-is.

Sources:
[Artboard context menu](https://helpx.adobe.com/illustrator/desktop/create-manage-artboards/organize-manage-artboards/use-artboard-context-menu.html) ·
[Group/ungroup](https://helpx.adobe.com/illustrator/desktop/manage-objects/select-objects/group-ungroup-objects.html) ·
[Isolate objects](https://helpx.adobe.com/illustrator/desktop/manage-objects/select-objects/isolate-objects.html) ·
[Layers panel overview](https://helpx.adobe.com/illustrator/desktop/manage-layers/create-and-organize-layers/layers-panel-overview.html) ·
[Illustrator default shortcuts](https://helpx.adobe.com/illustrator/using/default-keyboard-shortcuts.html) ·
[Figma: Copy and paste objects](https://help.figma.com/hc/en-us/articles/4409078832791-Copy-and-paste-objects) ·
[Figma: Copy and paste properties](https://help.figma.com/hc/en-us/articles/4412765442967-Copy-and-paste-properties-between-layers) ·
[Figma: Select layers and objects](https://help.figma.com/hc/en-us/articles/360040449873-Select-layers-and-objects) ·
[Figma: Identify matching objects](https://help.figma.com/hc/en-us/articles/21523793229463-Identify-matching-objects) ·
[Figma: Crop an image](https://help.figma.com/hc/en-us/articles/360040675194-Crop-an-image) ·
[Paper shortcuts](https://paper.design/docs/support)

---

## 3. Typography

### Illustrator: Character panel (Ctrl+T; Show Options shows every field)

| Control | Behaviour |
|---|---|
| Font family and style | A family combo and a separate style combo listing the font's actual faces (Light … Black, Condensed, and so on). The font menu has live previews, favourites, recently used fonts and filters. |
| Size | pt. Ctrl+Shift+. / , steps it (by 2 pt by default). |
| Leading | Absolute pt. **Auto = 120 % of size**, shown in parentheses. Leading is a character attribute; the largest value on a line wins. |
| Kerning | **Auto/Metrics** (the font's kern pairs, the default), **Optical** (from glyph shapes), or a **manual** value between two characters (only at an insertion point). 0 turns it off. Units are 1/1000 em. |
| Tracking | 1/1000 em, over a range; adds to kerning. |
| Vertical / horizontal scale | %. |
| Baseline shift | pt, positive is up. |
| Character rotation | degrees, per glyph. |
| Buttons | All Caps, Small Caps, Superscript, Subscript, Underline, Strikethrough. |
| Language | Sets the hyphenation and spelling dictionary. |
| Anti-aliasing | None / Sharp / Crisp / Strong (for raster export). |

**OpenType panel** (Alt+Shift+Ctrl+T): Standard ligatures, Contextual
alternates, Discretionary ligatures, Swash, Stylistic and Titling alternates,
Ordinals, Fractions and Stylistic sets. **Figures** can be tabular lining,
proportional lining, proportional oldstyle or tabular oldstyle. **Position**
can be superscript, subscript, numerator or denominator. Features the font
lacks are greyed out.

### Illustrator: Paragraph panel (Alt+Ctrl+T)

- **Alignment:** left, center and right, plus four justify options: last line
  left, center or right, or every line.
- **Indents:** left, right and first-line. A negative first-line indent makes
  a hanging indent.
- **Spacing:** space before and after. Space before is skipped for the first
  paragraph in a frame.
- **Hyphenate** checkbox, with options for minimum word length, minimum
  letters after the start and before the end, maximum consecutive hyphens,
  hyphenation zone, and whether to hyphenate capitalised words.
- **Justification settings:** minimum, desired and maximum word, letter and
  glyph spacing, and auto leading %.
- **Composer:** Every-line (balances breaks across several lines) or
  Single-line.
- **Also:** bullets and numbered lists, and Roman hanging punctuation.

### Illustrator: text objects and tools

- **Point type:** click and type; the line never wraps. **Area type:** drag a
  box or click inside a shape, and text wraps to it. Type ▸ Convert to Area /
  Point Type switches between them; the Contextual Task Bar also has the
  toggle.
- **Area Type Options:**
  - Rows and columns: number, span and gutter each, flowing by rows or by
    columns.
  - **Inset spacing.**
  - **First baseline:** Ascent, Cap Height, Leading, x Height, Em Box or
    Fixed, with a minimum.
- **Type on a path:**
  - Ctrl-drag the centre bracket to slide the text along the path; drag it
    across the path to flip.
  - Effects: Rainbow, Skew, 3D Ribbon, Stair Step, Gravity.
  - Align to path by ascender, descender, center or baseline.
- **Text wrap** (Object ▸ Text Wrap ▸ Make) flows area type around an object,
  with an offset.
- **Threaded text** between frames, using the overflow port.
- **Create Outlines** (Shift+Ctrl+O) converts text to paths.
- **Find/Replace Font:** lists the fonts used in the document and replaces them
  from the document's fonts, recent fonts or system fonts.
- **Character and Paragraph Styles:**
  - Create them from the selection; overrides show a "+" after the style name.
  - Load styles from another file.
- **Touch Type tool** (Shift+T): per-glyph scale, move and rotate handles.
- **Show Hidden Characters** (Alt+Ctrl+I), **Insert Whitespace** (em, en,
  thin, hair and non-breaking spaces), **Smart Punctuation**, **Change Case**.

### Illustrator: type shortcuts (Windows)

| Action | Keys |
|---|---|
| **Kerning / tracking** | **Alt+← / →** (×5 with Ctrl+Alt) |
| **Leading** | **Alt+↑ / ↓** |
| **Baseline shift** | **Alt+Shift+↑ / ↓** |
| Font size up / down | Ctrl+Shift+. / , |
| Reset kerning/tracking to 0 | Ctrl+Alt+Q |
| Reset horizontal scale | Ctrl+Shift+X |
| Align left / center / right | Ctrl+Shift+L / C / R |
| Justify / justify all | Ctrl+Shift+J / F |
| Superscript / subscript | Ctrl+Shift+= / Ctrl+Alt+Shift+= |
| Soft return | Shift+Enter |
| Show hidden characters | Alt+Ctrl+I |

The step sizes for size, leading, baseline shift and tracking are set in
Preferences ▸ Type. Common defaults are 2 pt, 2 pt, 2 pt and 20/1000 em; Adobe
doesn't state them.

### Figma: text properties (right sidebar, UI3)

- **Main fields:**
  - Font, weight/style and size.
  - **Line height:** Auto, px or %.
  - **Letter spacing:** % or px. With a mixed selection, changes are
    proportional.
  - **Paragraph spacing** in px.
  - **Paragraph indent:** first line only.
  - **Horizontal alignment:** left, center, right or justify.
  - **Vertical alignment:** top, middle or bottom, for fixed boxes only.
- **Resizing:**
  - **Auto width:** Illustrator's point type.
  - **Auto height:** a fixed width that grows downwards.
  - **Fixed size:** area type.
  - Clicking with the Text tool makes auto-width text; dragging makes a
    fixed-size box.
- **Overflow:**
  - **Truncate text** adds an ellipsis, with **Max lines**.
  - **Wrap style:** Auto, **Balance** (evens out line lengths) or **Pretty**
    (avoids orphans).
- **Type details ("…"):**
  - **Decoration:** underline (style, thickness, offset, skip-ink, colour) and
    strikethrough.
  - **Case:** upper, lower, title or small caps.
  - **Vertical trim:** crops the line box to cap height and baseline.
  - **Lists:** bulleted and numbered, with list spacing.
  - Hanging punctuation and lists.
  - **OpenType:** ligatures, stylistic sets, character variants, fractions,
    tabular/proportional and lining/oldstyle figures, slashed zero,
    superscript and subscript.
  - **Variable-font axes:** weight, width, optical size, slant and custom axes.
- **Text styles:** hold family, weight, size, line height, letter spacing,
  paragraph spacing and indent, decoration, case, lists and OpenType. They
  don't hold colour, alignment or resizing. Apply one to a layer or to a
  range, and detach it from its row.
- **Shortcuts (Mac → Win):**
  - Size: Ctrl+Shift+&lt; / &gt;.
  - **Letter spacing: Alt+&lt; / &gt;.**
  - **Line height: Alt+Shift+&lt; / &gt;.**
  - Bold, italic and underline: Ctrl+B / I / U.
  - Alignment: Ctrl+Alt+L / T / R / J.
- **Also:** text on a path (a start handle, and flip), and multi-edit (select
  several text layers and press Enter to type into all of them).

### Paper

- **Units:** letter spacing in **em**, line height in px or **Auto**, size in
  px. These are CSS units.
- **Fonts:** variable axes, optical sizing and OpenType. Local fonts come
  before Google Fonts.
- **Text panel:** case, wrapping and **truncation**. Text can take a stroke
  and a gradient fill.
- **Shortcuts:** the same as Figma's, using , and . instead of &lt; and &gt;:
  - Size: ⌘⇧, / .
  - **Weight: ⌥⌘, / .** (neither Illustrator nor Figma has a weight shortcut).
  - Letter spacing: ⌥, / .
  - Line height: ⌥⇧, / .

### Takeaways for Omastrator

- **Units:** tracking and kerning in **1/1000 em**, as Illustrator does. It
  scales with size, and it's what Omastrator's audience expects. Show % or em
  as a hint. Leading goes in **pt, with an Auto (120 %) option**. Today's
  `leading` is a multiplier and `tracking` is absolute pt, so both need a
  model change: store em/1000 and keep the old keys readable.
- **Where it lives:** a **Character section in the Properties panel** whenever
  text is selected, not just while the Type tool is active. It needs family, a
  **real style combo** (`QFontDatabase::styles`), size, leading, kerning mode,
  tracking, the case buttons and alignment. Advanced fields go behind a "…"
  button, as Illustrator's Show Options and Figma's Type details do.
- **Keys:** Alt+←/→ for tracking (and kerning at a caret), Alt+↑/↓ for leading,
  Alt+Shift+↑/↓ for baseline shift, and Ctrl+Shift+. / , for size. These match
  Illustrator and don't clash with anything bound today.
- **Area type** (Figma's "Fixed width / Auto height") is the most-used missing
  text feature, and `QTextLayout` handles wrapping and justification.
- **Styled runs** are needed for manual kerning, per-word styling, and
  superscript on part of a line. This is an architectural change
  (`TextContent` becomes paragraphs of runs). Plan it deliberately, as P1, not
  by accident.

Sources:
[Character panel](https://helpx.adobe.com/illustrator/desktop/design-with-text/edit-format-text/character-panel-overview.html) ·
[Kerning and tracking](https://helpx.adobe.com/illustrator/desktop/design-with-text/edit-format-text/adjust-kerning-and-tracking.html) ·
[Line and character spacing](https://helpx.adobe.com/illustrator/using/line-character-spacing.html) ·
[Case and capitalization](https://helpx.adobe.com/illustrator/desktop/design-with-text/edit-format-text/change-case-and-capitalization-styles.html) ·
[Paragraph panel](https://helpx.adobe.com/illustrator/desktop/design-with-text/edit-format-text/paragraph-panel-overview.html) ·
[Indent text](https://helpx.adobe.com/illustrator/desktop/design-with-text/edit-format-text/indent-text.html) ·
[OpenType panel](https://helpx.adobe.com/illustrator/desktop/design-with-text/special-characters-glyphs/opentype-panel-overview.html) ·
[Hyphenation and line breaks](https://helpx.adobe.com/illustrator/using/hyphenation-line-breaks.html) ·
[Area type margins and first baseline](https://helpx.adobe.com/illustrator/desktop/design-with-text/add-manage-text/add-margins-to-text-and-adjust-the-first-baseline.html) ·
[Type on a path](https://helpx.adobe.com/illustrator/using/creating-type-path.html) ·
[Text wrap](https://helpx.adobe.com/illustrator/desktop/design-with-text/add-manage-text/wrap-or-unwrap-text-around-objects.html) ·
[Find and replace fonts](https://helpx.adobe.com/illustrator/desktop/design-with-text/fonts-and-scripts/find-and-replace-fonts.html) ·
[Character and paragraph styles](https://helpx.adobe.com/illustrator/using/character-paragraph-styles.html) ·
[Touch Type tool](https://helpx.adobe.com/illustrator/using/tool-techniques/touch-type-tool.html) ·
[Figma: Explore text properties](https://help.figma.com/hc/en-us/articles/360039956634-Explore-text-properties) ·
[Figma: Text styles](https://help.figma.com/hc/en-us/articles/360039957034-Create-and-apply-text-styles) ·
[Figma: Guide to text](https://help.figma.com/hc/en-us/articles/360039956434-Guide-to-text-in-Figma-Design) ·
[Paper docs](https://paper.design/docs) · [Paper shortcuts](https://paper.design/docs/support)

---

## 4. Detail-oriented and QoL features

### Precision and measurement

- **Smart guides** (Illustrator, Ctrl+U). Each of these can be switched on or
  off:
  - Alignment guides and object highlighting.
  - **Measurement labels:** cursor X/Y, and Δx/Δy while dragging.
  - Transform readouts (W/H while scaling, the angle while rotating).
  - Spacing guides and distance guides.
  - **Anchor and path labels** ("anchor", "path", "center" under the cursor).
  - **Construction guides** at angles you set.
  - A snapping tolerance in pt.
  - A 2025 release added snapping to endpoints, midpoints and centers.
- **Figma measure:** select an object, then **hold Alt and hover** another. Red
  lines show the H/V distances between their bounding boxes; Ctrl+Alt measures
  outside the parent. Paper does the same.
- **Snapping:**
  - Illustrator has Snap to Point (Alt+Ctrl+'), Snap to Grid (Shift+Ctrl+'),
    **Snap to Pixel** (whole pixels while drawing, moving and scaling), and
    Object ▸ Make Pixel Perfect.
  - Figma has a pixel grid at ≥400 % zoom and **Snap to pixel grid**
    (Ctrl+Shift+').
- **Rulers and guides** (Illustrator):
  - Ctrl+R shows rulers; drag a guide out of one.
  - Hide Guides Ctrl+;, Lock Guides Alt+Ctrl+;.
  - **Make Guides from a path** (Ctrl+5) and Release Guides (Alt+Ctrl+5).
  - Alt-drag switches a guide between horizontal and vertical.
  - Clear Guides removes them all.
- **Number fields:**
  - Illustrator accepts **units and math**: `10mm`, `3cm*50%`, `50pt+25%`.
    ↑/↓ step by 1 and Shift by 10.
  - Figma supports `+ - * / ^ ( )`, including relative values over a mixed
    selection (`+10`, `*2`), and **scrubs when you drag the field label**.
- **Reference point:**
  - Illustrator's Transform panel has a **9-point reference locator**, a
    **constrain proportions** link, and **Scale Corners** and **Scale Strokes
    & Effects** checkboxes.
  - Ctrl+Enter in a W/H field keeps proportions.
- **Nudge:**
  - Illustrator uses Preferences ▸ General ▸ Keyboard Increment (default 1 pt,
    ×10 with Shift).
  - Figma has a small nudge (1) and a big nudge (10), both configurable.
  - Paper's big nudge is 8 by default.
  - Illustrator's **Alt+arrow duplicates and nudges**.

### Alignment and layout

- **Align** (Illustrator):
  - Align To can be Selection, **Key Object** or Artboard. Clicking an
    already-selected object makes it the key object and draws it with a
    thick outline.
  - **Distribute Spacing** takes an exact gap in pt, measured from the key
    object.
  - With Direct Selection, align works on anchor points.
- **Figma:**
  - Alt+A/D/W/S/H/V to align, and **Tidy up** (Ctrl+Alt+T) to snap loose
    objects into rows or a grid.
  - **Smart selection:** pink spacing handles let you drag to change the gap,
    drag to reorder, and Shift-scroll to adjust.
- **Auto layout** (Figma) and flex (Paper): stacks with gap, per-side
  padding, hug/fill/fixed sizing and wrapping. Useful for UI mock-ups, but
  outside an illustration tool's core. See P2.

### Paint and appearance

- **Appearance panel** (Illustrator):
  - Multiple fills and strokes per object, stacked, each with its own opacity
    and blend mode, and an eye to hide it.
  - Add New Fill/Stroke, Duplicate, Clear Appearance.
  - Figma stacks fills and strokes the same way.
- **Stroke:**
  - **Align stroke:** center, inside or outside (closed paths).
  - Dash options: **preserve exact lengths** vs **align dashes to corners and
    ends**, with up to 3 dash/gap pairs.
  - **Arrowheads:** start and end, scale, tip at or beyond the end.
  - **Width profiles** and the Width tool (Shift+W).
  - Figma adds **per-side strokes** on rectangles and **arrow caps**.
- **Corners:**
  - Illustrator's Live Corners are on-canvas widgets; Alt-click cycles
    round, inverted round and chamfer. Double-click opens the Corners dialog.
  - Figma has **independent corners** (one radius per corner) and **corner
    smoothing** (squircle; an "iOS" 60 % preset).
- **Colour:**
  - The Color panel has a **hex field**, RGB/HSB, and Invert/Complement.
  - **Global swatches** push edits to every use.
  - Swatch groups.
  - **Recent colours** (Figma shows document colours in the picker).
  - Figma's **Selection colors** lists every colour in a mixed selection,
    lets you edit each one, and can select all layers using it.
- **Eyedropper:**
  - Shift samples colour only.
  - **Alt-click applies to an unselected object.**
  - Dragging outside the document samples the screen.
  - The tool's options set which attributes it picks up and applies.
- **Gradients:**
  - An **on-canvas annotator** (origin, end, stops, midpoints, angle and
    aspect for radial).
  - Freeform gradients.
- **Transparency:** opacity masks (Make Mask, Clip, Invert), isolate blending
  and knockout group.

### Path editing

- **Shape Builder** (Shift+M): drag across regions to merge them, Alt-click to
  remove. The most loved Illustrator tool of the last decade.
- **Join** (Ctrl+J) connects two open endpoints. **Average** (Alt+Ctrl+J)
  lines up anchors horizontally, vertically or both.
- **Scissors** (C) and **Knife** cut paths.
- Simplify with a live preview and point counts.
- Offset Path with joins and a miter limit.
- **Reverse path direction** and the **even-odd / non-zero fill rule**.
- Figma: vector networks, a **Bend tool**, non-destructive **boolean groups**,
  and Flatten.

### Selection, visibility and organisation

- **Select menu** (Illustrator):
  - **Select ▸ Same:** fill colour, stroke colour, stroke weight, fill &
    stroke, opacity, blending mode, and font family, style or size.
  - **Inverse, Next Object Above or Below**, and Object ▸ Text Objects /
    Clipping Masks / Stray Points.
  - **Save Selection.**
  - Figma's equivalent is **Select matching layers**.
- **Lock and hide:**
  - Ctrl+2 locks and Ctrl+3 hides; with Alt they unlock or show all.
  - **Lock Others** is Ctrl+Alt+Shift+2 and **Hide Others** is
    Ctrl+Alt+Shift+3.
- **Isolation mode:** double-click a group to edit inside it while everything
  else is dimmed. A breadcrumb bar sits on top, and Esc leaves.
- **Outline mode:** Ctrl+Y for the document; per layer with a Ctrl-click on its
  eye.

### Clipboard and repetition

- **Paste in Front** (Ctrl+F), **Paste in Back** (Ctrl+B), Paste in Place
  (Shift+Ctrl+V) and Paste on All Artboards.
- Figma adds **Paste to replace** (Ctrl+Shift+R) and **Paste over selection**.
- **Transform Again** (Ctrl+D) repeats the last move, rotate or scale,
  **including Alt-drag duplicates**. That makes step-and-repeat
  (Alt-drag, Ctrl+D, Ctrl+D…) possible.
- Figma's Ctrl+D duplicate repeats the last offset.
- Object ▸ Repeat: radial, grid and mirror, kept live.

### Views and history

- **Zoom:**
  - Illustrator: Ctrl+0 fits the artboard, Ctrl+Alt+0 fits all artboards,
    Ctrl+1 goes to 100 %.
  - Figma: **Shift+1 zoom to fit, Shift+2 zoom to selection**, Shift+0 goes
    to 100 %.
- **History panel:** a list of named states; click one to go back. Illustrator
  keeps 100 by default.
- **Artboards:**
  - Several per document, with names.
  - The Artboard tool (Shift+O), with "+" handles that add a same-size
    artboard.
  - **Fit to Artwork Bounds.**
  - Export per artboard.

### Export and reuse

- **Export for Screens** (Illustrator):
  - Export artboards or collected assets.
  - **Scales 0.5×–3×** and suffixes; PNG, JPG, SVG, PDF and WebP.
  - Right-click ▸ Collect for Export.
- Figma has a **per-layer Export section** (format, scale, suffix).
- **Symbols** (Illustrator) and **components/instances** (Figma): edit the
  master and every instance updates.

Sources:
[Smart Guides](https://helpx.adobe.com/illustrator/desktop/measure-and-align/grids-and-guides/work-with-smart-guides.html) ·
[Rulers, grids, guides](https://helpx.adobe.com/illustrator/using/rulers-grids-guides-crop-marks.html) ·
[Pixel-perfect](https://helpx.adobe.com/illustrator/using/pixel-perfect.html) ·
[Enter values in panels](https://helpx.adobe.com/illustrator/desktop/get-started/learn-the-basics/enter-values-in-panels-and-dialog-boxes.html) ·
[Transforming objects](https://helpx.adobe.com/illustrator/using/transforming-objects.html) ·
[Align and distribute](https://helpx.adobe.com/illustrator/desktop/manage-objects/arrange-objects/align-and-distribute-objects.html) ·
[Distribute by distance](https://helpx.adobe.com/illustrator/desktop/manage-objects/arrange-objects/distribute-objects-by-specific-distances.html) ·
[Multiple fills and strokes](https://helpx.adobe.com/illustrator/desktop/paint-and-fill/learn-painting-basics/create-multiple-fills-and-strokes.html) ·
[Stroke](https://helpx.adobe.com/illustrator/using/stroke-object.html) ·
[Arrowheads](https://helpx.adobe.com/illustrator/desktop/paint-and-fill/apply-and-edit-strokes/add-arrowheads.html) ·
[Width tool](https://helpx.adobe.com/illustrator/using/tool-techniques/width-tool.html) ·
[Live shapes](https://helpx.adobe.com/illustrator/using/live-shapes.html) ·
[Shape Builder](https://helpx.adobe.com/illustrator/using/tool-techniques/shape-builder-tool.html) ·
[Average anchors](https://helpx.adobe.com/illustrator/desktop/draw-shapes-and-paths/modify-paths/average-the-position-of-anchor-points.html) ·
[Cutting and dividing](https://helpx.adobe.com/illustrator/using/cutting-dividing-objects.html) ·
[Eyedropper](https://helpx.adobe.com/illustrator/using/tool-techniques/eyedropper-tool.html) ·
[Gradients](https://helpx.adobe.com/illustrator/using/gradients.html) ·
[Swatches](https://helpx.adobe.com/illustrator/using/using-creating-swatches.html) ·
[Transparency and blending](https://helpx.adobe.com/illustrator/using/transparency-blending-modes.html) ·
[Select same](https://helpx.adobe.com/illustrator/desktop/manage-objects/select-objects/select-objects-by-characteristics.html) ·
[Paste in front/back](https://helpx.adobe.com/illustrator/desktop/manage-objects/arrange-objects/move-or-duplicate-an-object-by-pasting.html) ·
[History / undo](https://helpx.adobe.com/illustrator/using/recovery-undo-automation.html) ·
[Multiple artboards](https://helpx.adobe.com/illustrator/using/using-multiple-artboards.html) ·
[Export for Screens](https://helpx.adobe.com/illustrator/using/collect-assets-export-for-screens.html) ·
[Figma: Measure distances](https://help.figma.com/hc/en-us/articles/360039956974-Measure-distances-between-layers) ·
[Figma: Zoom and view options](https://help.figma.com/hc/en-us/articles/360041065034-Adjust-your-zoom-and-view-options) ·
[Figma: Smart selection](https://help.figma.com/hc/en-us/articles/360040450233-Arrange-layers-with-Smart-Selection) ·
[Figma: Alignment, position, dimensions](https://help.figma.com/hc/en-us/articles/360039956914-Adjust-alignment-rotation-position-and-dimensions) ·
[Figma: Corner radius and smoothing](https://help.figma.com/hc/en-us/articles/360050986854-Adjust-corner-radius-and-smoothing) ·
[Figma: Stroke properties](https://help.figma.com/hc/en-us/articles/360049283914-Apply-and-adjust-stroke-properties) ·
[Figma: Boolean operations](https://help.figma.com/hc/en-us/articles/360039957534-Boolean-operations) ·
[Figma: Auto layout](https://help.figma.com/hc/en-us/articles/360040451373-Guide-to-auto-layout) ·
[Figma: Version history](https://help.figma.com/hc/en-us/articles/360038006754-View-a-file-s-version-history)

---

## 5. Paper: what it does differently

[paper.design](https://paper.design) calls itself "the connected canvas for
teams shipping with agents". It runs in the browser and as a desktop app, with
a Linux AppImage.

- **The canvas is real HTML/CSS.** Frames are flexbox (Shift+A wraps the
  selection in flex), and styles are CSS: filters, backdrop filters, shadows
  and outlines. Nothing needs translating to reach code: it exports React and
  Tailwind, and "copy as Tailwind" is Alt+T.
- **Colour:**
  - An OKLCH colour picker; sRGB and Display P3 can be mixed per element.
  - Gradients are edited on the canvas, with snap points.
- **Tokens are CSS variables** and map to Tailwind: colour, radius, spacing,
  font family, weight, size, line height and letter spacing. They're managed
  in a Theme tab, and Backspace in a field detaches a token.
- **Paper Shaders:** an open-source library of mesh gradients, grain,
  dithering, halftone, liquid metal and more, which can be applied as fills.
- **Built for agents:** the desktop app runs a local MCP server that agents
  read and write as HTML. Omastrator's own MCP bridge is similar.
- **Canvas UX:**
  - **Hold Alt to measure.**
  - **⌘-right-click lists every layer under the pointer.**
  - **Selection:** Enter selects children, Esc or Shift+Enter selects the
    parent, and Tab cycles siblings. A selection brush selects what it fully
    encloses.
  - **Typing 0–9 sets opacity** (5 = 50 %, 05 = 5 %). Figma does the same.
  - ⌘+arrow resizes the selection.
  - Nudge is 1, or **8** with Shift, and both are configurable.
  - Shift+X swaps fill and stroke; I is the eyedropper.
  - ⌘. hides the UI.
- **Typography:** letter spacing in em, line height Auto or px, variable-font
  axes, and ⌥⌘, / . for **weight**.

What Omastrator should borrow from Paper:

- **Keyboard opacity (0–9).**
- **Alt-hover measuring.**
- **A layer picker in the right-click menu.**
- **An OKLCH/hex-first colour field.**
- **Configurable nudge.**

Its HTML-native model and CSS tokens overlap with Omastrator's Live feature
rather than the core editor.

Sources: [paper.design](https://paper.design) ·
[Docs](https://paper.design/docs) · [Shortcuts](https://paper.design/docs/support) ·
[MCP](https://paper.design/docs/mcp) · [Tokens](https://paper.design/docs/tokens) ·
[Build log](https://paper.design/build-log) · [Roadmap](https://paper.design/roadmap)

---

## 6. UI polish patterns

- **Illustrator Properties panel (2020+):**
  - Nothing selected: document settings (artboard, units, rulers, grid,
    guides, snapping) and Quick Actions (Document Setup, Preferences, Edit
    Artboards).
  - Something selected: Transform, Appearance, then **type-specific
    sections**. Text gets Character and Paragraph; images get Crop, Embed and
    Image Trace; groups get Isolate.
  - **Quick Actions** at the bottom: Group, Arrange, Start Global Edit,
    Recolor.
  - Underlined labels and "…" open the full panel.
- **Contextual Task Bar (Illustrator):** a small floating bar near the
  selection. It can be pinned, moved or hidden.
  - **Path:** fill and stroke; an open path adds Join Path and Edit Path; a
    closed shape adds Lock.
  - **Several objects:** Group, Align, Recolor.
  - **Group:** Ungroup, Isolate.
  - **Text:** Point ⇄ Area type, Type on a Path, Outline.
  - **Direct Selection:** Simplify, Smooth, Remove anchors, Connect endpoints,
    Cut at anchors, Corner/Smooth.
  - **Image:** Image Trace, Crop, Embed.
  - **Clipping mask:** Edit mask, Release.
- **Figma UI3:**
  - The toolbar sits at the bottom of the canvas, and panels float and resize.
  - Inputs have filled backgrounds, and property labels are optional.
  - The right sidebar runs in a fixed order: position → layout (W/H) →
    appearance (opacity, radius) → typography → fill → stroke → effects →
    export.
  - A **"Minimize UI"** toggle collapses the panels.
- **Command palette:** Figma's **Actions menu (Ctrl+K, Ctrl+/)** searches every
  command, with its shortcut, plus plugins and assets. Illustrator's
  **Discover panel** search (F1) shows coach marks pointing to where a tool
  lives.
- **Tooltips:** Illustrator's tooltips show the tool name and shortcut, and
  "rich tooltips" add a short animation. Figma shows the shortcut in every
  tooltip. Both have a keyboard shortcuts overlay (Figma Ctrl+Shift+?).
- **Toolbar:** Illustrator has Basic and Advanced presets, a "…" drawer to add
  or remove tools, grouped flyouts, and Alt-click to cycle the hidden tools in
  a group.
- **Empty states:** Illustrator shows document quick actions when nothing is
  selected, and Figma shows page and file properties. Neither shows a blank
  panel.

Sources:
[Properties panel](https://helpx.adobe.com/illustrator/desktop/get-started/learn-the-basics/properties-panel-overview.html) ·
[Contextual Task Bar](https://helpx.adobe.com/illustrator/desktop/get-started/learn-the-basics/contextual-task-bar-overview.html) ·
[Toolbar](https://helpx.adobe.com/illustrator/using/tools.html) ·
[Workspace basics](https://helpx.adobe.com/illustrator/using/workspace-basics.html) ·
[Discover panel](https://helpx.adobe.com/illustrator/desktop/get-started/learn-the-basics/learn-with-discover-panel.html) ·
[Figma: Right sidebar](https://help.figma.com/hc/en-us/articles/360039832014-Design-prototype-and-explore-layer-properties-in-the-right-sidebar) ·
[Figma: Toolbar](https://help.figma.com/hc/en-us/articles/360041064174-Access-design-tools-from-the-toolbar) ·
[Figma: Actions menu](https://help.figma.com/hc/en-us/articles/23570416033943-Use-the-actions-menu-in-Figma-Design) ·
[Figma: Hide or minimize the UI](https://help.figma.com/hc/en-us/articles/41414918021271-Hide-or-minimize-the-UI) ·
[Figma: Behind UI3](https://www.figma.com/blog/behind-our-redesign-ui3/)

---

## 7. UI polish for Omastrator's existing panels

Taken from `hero.png`, `type-on-canvas.png`, `pathfinder-before.png`,
`swatches.png` and `selection-smart-guides.png`.

> **Done: 1–8 and 11**, as below. Differences: Align shows with any selection
> (one object aligns to the artboard); sizes are set in the Properties panel,
> not the whole theme; Swatches stays a floating panel that opens beside the
> dock, not over it, without the hex tooltips, recent colours or Add button;
> Selection colors and the edge-distribute buttons stay P1. Not in this
> round: 9 (status bar), 10 (tool rail groups) and 12 (hover and focus states).
> Tests: `tests/UI/PropertiesPanelTests.cpp`, `tests/UI/ContentViewTests.cpp`,
> `tests/UI/FloatingPanelTests.cpp`, `tests/UI/ColorPaletteControlsTests.cpp`,
> `tests/Canvas/EditorCanvasTests.cpp`.

1. **Properties and Layers fight for one column** (`hero.png`,
   `swatches.png`). Properties is cut off after Blend, and the "Stroke"
   heading shows half-hidden above the splitter grip. Change:
   - Give the dock a real `QSplitter` handle: a visible grip, a remembered
     position, and a double-click to reset.
   - Make each section collapsible, with a chevron on its heading and the
     open/closed state saved per section in `QSettings`.
   - Show only the sections that apply: no Align with one object unless the
     target is Artboard, and no Pathfinder with fewer than two objects.
2. **Combo boxes are visibly larger than the labels and fields**: "Radial",
   "None", "Normal", "Butt", "Miter" and "Align to Selection" in
   `pathfinder-before.png`. Change: one control height (26 px) and one font
   size for `QComboBox`, `QLineEdit` and `NumberField` in `OmarchyTheme`'s
   stylesheet, and caption-sized dropdown text.
3. **The rotation label "θ" is a faint glyph that's hard to find**
   (`hero.png`). Change:
   - Use a rotate icon as the field label, as Figma does.
   - Add a **constrain-proportions link** between W and H.
   - Add a **9-point reference locator** to the left of X/Y.
   - Allow **scrubbing** by dragging the X/Y/W/H/° labels.
4. **Type controls exist only in the header while the Type tool is active**
   (`type-on-canvas.png`). When a text object is selected with the Selection
   tool, Properties shows only Transform and Appearance. Change: a
   **Character** section in Properties (see P0-2), with the header bar kept as
   a compact copy.
5. **New text layers are named after their first keystroke**: the "N" row in
   `type-on-canvas.png`. Change: text layers without a custom name take their
   content as the name, trimmed to about 30 characters and updated live, as
   Illustrator does. Store "has custom name" so a rename sticks.
6. **Mixed values aren't shown.** With four differently coloured shapes
   selected (`pathfinder-before.png`), Fill shows one blue swatch and "Solid".
   Change: show a "Mixed" swatch (diagonal hatching) and blank number fields
   with the placeholder "Mixed", as Figma and Illustrator do. For fills, list
   the distinct colours as **Selection colors** (P1).
7. **Pathfinder and Align are unlabelled icons.** Change: every icon button
   gets a tooltip with its name and shortcut, such as "Unite (Pathfinder)" or
   "Align left edges — Alt+A", and a hover background. Add Distribute left,
   right, top and bottom, plus a **Distribute spacing** field (P1), next to the
   two centre-distribute buttons.
8. **The swatches popover covers the Properties panel** (`swatches.png`).
   Change: dock Swatches as a collapsible Properties section, or anchor the
   popover to the left of the dock. Show the hex value in each swatch's
   tooltip, add a recent-colours row, and add an "Add selected colour" button.
9. **The status bar shows "X – Y –" when the pointer is off the canvas.**
   Change: show the selection's X/Y/W/H instead, and let a click on the
   zoom % open a zoom menu (Fit, Selection, 50/100/200 %).
10. **The tool rail has no groups or flyouts.** It holds 16 tools in one
    column, and the flame button sits at the bottom with nothing to separate
    it. Change:
    - Group the shape tools into one flyout (rectangle ▸ rounded rectangle,
      ellipse, polygon, star), which leaves room for Shape Builder, Scissors
      and Artboard.
    - Alt-click cycles the tools in a group.
    - Tooltips show "Rectangle (M)".
11. **Empty Properties state.** With nothing selected, show document settings
    (artboard size and background, units, grid and snapping toggles, nudge
    amount) and quick actions (Document Setup, Export, Fit Artboard). Today
    only the artboard W/H shows.
12. **Hover and focus states.** Layer rows, icon buttons and swatches should
    all have a hover fill. Focus rings should use the theme accent. The Layers
    eye and lock should be dimmed until hover, except when a layer is hidden or
    locked (Figma shows them on hover only).

---

## 8. Gap analysis and prioritized build plan

These rules apply to every item: edits go through `EditorSession` as one named
undo step; new actions are named `QAction`s in `Menus` so they are remappable
and appear in menus, context menus and the command palette; and every item
has Qt Test coverage in `tests/<Folder>/`.

### Gap summary

| Capability | Illustrator | Figma | Omastrator | Priority |
|---|---|---|---|---|
| Canvas and Layers right-click menus | ✓ | ✓ | ✓ done | **P0** |
| Tracking, leading and kerning UI + shortcuts | ✓ | ✓ | ✓ done | **P0** |
| Font style/weight picker | ✓ | ✓ | ✓ done | **P0** |
| Case, underline and strikethrough, baseline shift, scale | ✓ | ✓ | ✓ done (per object) | **P0** |
| Area type (fixed width, wrapping, justify) | ✓ | ✓ | ✓ done | **P0** |
| Math and units in number fields, scrubbing | ✓ | ✓ | ✓ done | **P0** |
| Reference point and constrain proportions | ✓ | ✓ (link) | ✓ done | **P0** |
| Paste in Front/Back, Transform Again (Ctrl+D) | ✓ | (Ctrl+D smart duplicate) | ✓ done | **P0** |
| Select menu: Same ▸, Inverse, Next Above/Below | ✓ | select matching | ✓ done | **P0** |
| Alt-hover distance measuring | (smart guides) | ✓ | ✓ done | **P0** |
| Zoom to selection | ✓ | ✓ Shift+2 | ✓ done | **P0** |
| Configurable nudge, Alt+arrow duplicate | ✓ | ✓ | ✓ done | **P0** |
| Rulers and guides | ✓ | ✓ | ✓ done | P1 |
| Multiple fills and strokes | ✓ | ✓ | ✓ done | P1 |
| Stroke align, arrowheads | ✓ | ✓ | ✓ done | P1 |
| Per-corner radius / live corners | ✓ | ✓ | ✓ done | P1 |
| Styled runs (per-character styles, manual kerning) | ✓ | ✓ | ✓ done | P1 |
| OpenType features | ✓ | ✓ | ✓ done | P1 |
| Paragraph indents and spacing | ✓ | ✓ | ✓ done | P1 |
| Character/paragraph/text styles | ✓ | ✓ | ✓ done | P1 |
| Isolation mode | ✓ | (enter group) | ✓ done | P1 |
| Join, Average, Scissors, Reverse path | ✓ | ✓ | ✓ done | P1 |
| Selection colours, recent colours, hex field | ✓ | ✓ | ✓ done | P1 |
| Copy/Paste properties | (eyedropper) | ✓ | ✓ done | P1 |
| Command palette | Discover | ✓ Ctrl+K | ✓ done | P1 |
| History panel | ✓ | version history | ✓ done | P1 |
| Key object, distribute spacing | ✓ | (smart selection) | ✓ done | P1 |
| Snap to pixel, pixel grid | ✓ | ✓ | ✓ done | P2 |
| Collapsible Properties sections, contextual task bar | ✓ | ✓ | ✓ done | P1 |
| Multiple artboards | ✓ | frames | ✗ | P2 |
| Shape Builder | ✓ | ✗ | ✗ | P2 |
| Type on a path, text wrap, threads, hyphenation | ✓ | partial | ✗ | P2 |
| Symbols/components | ✓ | ✓ | ✗ | P2 |
| Width tool / variable strokes | ✓ | ✓ | ✗ | P2 |
| On-canvas gradient annotator | ✓ | ✓ | ✓ done | P2 |
| Opacity masks | ✓ | masks | clip only | P2 |
| Per-object export / Export for Screens | ✓ | ✓ | whole document | P2 |
| Auto layout | ✗ | ✓ | ✗ | out of scope |

### P0: must-have quality of life

**P0-1. Right-click context menus (canvas, Layers panel)**

> **Done.** `src/UI/ContextMenus.cpp`, `EditorCanvas+Menu.cpp`. Kept short, per
> VISION.md: **Ask AI…** first (Edit with Instruction, scoped to what was
> clicked), then a **Select ▸** picker when objects are stacked, the clipboard,
> what the selection's kind is for, and Arrange, Transform, Align, Pathfinder,
> Path and Select Same as submenus. Entries that can't apply are left out, not
> greyed. Undo and Redo, Copy/Paste Properties, New Sublayer, Show All, Unlock
> All and Merge Selected aren't in the menus. Tests: `tests/UI/ContextMenusTests.cpp`.

- **Spec:**
  - A right-click on the canvas hit-tests first. An unselected object under
    the pointer becomes the selection, as in Illustrator and Figma; a click
    inside the selection keeps it.
  - The menu then shows sections that depend on what's selected:
    - **Always:** Undo *name* / Redo *name*, then Cut, Copy, Paste, Paste in
      Place, Paste in Front, Paste in Back, Delete.
    - **Any selection:** Transform ▸ (Transform Again, Move…, Rotate…,
      Reflect…, Scale…, Flip Horizontal, Flip Vertical); Arrange ▸;
      **Select ▸** (Same ▸ …, Next Object Above, Next Object Below); Lock
      Selection; Hide Selection; Copy Properties; Paste Properties.
    - **Two or more objects:** Group, Make Compound Path, Make Clipping Mask,
      Align ▸, Pathfinder ▸.
    - **Group:** Ungroup, Isolate Selected Group.
    - **Compound path:** Release Compound Path.
    - **Clip group:** Release Clipping Mask.
    - **Path:** Outline Stroke, Offset Path…, Simplify, Reverse Path
      Direction (once it exists).
    - **Text:** Create Outlines, Convert to Area/Point Type, Font ▸ (recent
      fonts).
    - **Image:** Image Trace ▸, Vectorize with AI…
    - **Direct Selection with anchors picked:** Delete Anchor, Join, Average…,
      Convert to Corner/Smooth.
  - A **"Select Layer ▸"** submenu lists every object under the pointer,
    topmost first, with its kind icon; choosing one selects it.
  - **Empty canvas:** Paste, Paste in Place, Select All, then Zoom In, Zoom
    Out, Fit Artboard, Zoom to Selection, then Show Grid, Snap to Grid, Smart
    Guides, Outline, then Artboard Size…
  - **Inside inline text editing:** Cut, Copy, Paste, Select All, then
    Change Case ▸ (UPPERCASE, lowercase, Title Case, Sentence case), then
    Insert ▸ (em space, en space, thin space, non-breaking space, em dash, en
    dash, ellipsis, forced line break).
  - **Layers row:** Rename, Duplicate, Delete, New Layer, New Sublayer;
    Show/Hide, Lock/Unlock, Hide Others, Lock Others, Show All, Unlock All;
    Group, Ungroup, Make Clipping Mask; Select Children; Locate on Canvas
    (zoom to it); and, on layers, Layer Color ▸ and Merge Selected.
  - Alt-click an eye hides the others; Alt-click a lock locks the others.
- **Code:**
  - A new `src/UI/ContextMenus.{h,cpp}`: `ContextMenus::forCanvas(Menus&,
    EditorSession&, QPointF docPoint)` and `forLayerRow(...)`. They reuse
    `Menus::action(name)` so shortcuts and remaps show, and add gates in
    `Menus+Gates.cpp`.
  - `EditorCanvas::contextMenuEvent` in a new `EditorCanvas+Menu.cpp`.
  - `NativeLayerList` gets a `customContextMenuRequested` handler, and
    `NativeLayerList+Cell.cpp` gets the Alt-click modifiers.
  - `VectorDocument::hitTestAll(point, tolerance)` returns a
    `std::vector<QUuid>`.
  - New `EditorSession` calls: `lockOthers`, `hideOthers`, `selectSame`,
    `selectNextAbove/Below`, and `pasteInFront/Back`.
- **Accept:**
  - A right-click on an unselected object selects it and opens a menu whose
    items match its kind (tests for path, group, text, image, several objects
    and the empty canvas).
  - Every item shows the same shortcut as the menu bar, including after a
    remap.
  - Select Layer ▸ lists stacked objects in z-order.
  - A right-click on a Layers row selects that row and shows the row menu.
  - Alt-click on an eye hides every other layer in one undo step.
  - Esc closes the menu without changing anything.

**P0-2. A Character section in Properties, with tracking, kerning, leading and more**

> **Done.** `src/UI/CharacterSection.cpp`, `Document/TextContent.cpp`,
> `Document/TextLayout.cpp`. Family, the family's real styles, size, leading
> (Auto shows its value), tracking in 1/1000 em and five alignments are always
> there; kerning, baseline shift, horizontal and vertical scale, case,
> underline, strikethrough and point/area type sit behind a remembered **Show
> more**. `.omai` is version 2: old files' bold/italic become a style, tracking
> in pt becomes 1/1000 em, and a leading multiple becomes pt (1.2 is Auto).
> Canvas, SVG and PDF draw from one layout. Differences: kerning is Auto
> (the font's pairs) or None, as planned, with manual pairs from Alt+←/→ at a
> caret; there's no Optical and no Language. Tests:
> `tests/Document/TypographyTests.cpp`, `tests/UI/PropertiesPanelTests.cpp`,
> `tests/IO/SvgRoundTripTests.cpp`, `tests/IO/DocumentExporterTests.cpp`.

- **Spec:**
  - When the selection holds text (or the Type tool is active), Properties
    shows a **Character** section:
    - Family combo, plus a **Style combo** filled from
      `QFontDatabase::styles(family)` (Thin … Black, Italic, Condensed), which
      replaces the B/I flags.
    - Size.
    - **Leading** in pt, with an **Auto** option (120 %).
    - **Kerning:** Metrics / Optical* / 0.
    - **Tracking** in 1/1000 em.
    - Horizontal scale % and vertical scale %.
    - **Baseline shift** in pt.
    - Toggle buttons for **All Caps, Small Caps, Underline and Strikethrough**.
  - A **Paragraph** row holds left / center / right / justify-left /
    justify-all alignment.
  - A "…" button shows the advanced rows (scales, baseline shift, language).
  - Mixed values across several text objects show blank fields with the
    placeholder "Mixed".
  - *Optical kerning isn't available in Qt. Offer Metrics and None (0) in P0,
    and add Optical later only if a shaping backend supports it.*
- **Model:**
  - `TextContent` gains:
    - `QString style`, replacing `bold`/`italic`, which are migrated on load.
    - `double trackingEm` (1/1000 em), replacing absolute `tracking`: old
      files convert `tracking/size*1000`.
    - `std::optional<double> leadingPt` (nullopt = Auto 120 %), replacing the
      `leading` multiplier, which is migrated.
    - `bool kerning = true`.
    - `double horizontalScale = 100, verticalScale = 100, baselineShift = 0`.
    - `TextCase textCase {normal, allCaps, smallCaps}`.
    - `bool underline, strikethrough`.
  - `TextContent::font()` maps these to `QFont::setStyleName`,
    `setLetterSpacing(AbsoluteSpacing, size*em/1000)`, `setKerning`,
    `setCapitalization`, `setStretch` for horizontal scale (or a transform for
    arbitrary %), `setUnderline` and `setStrikeOut`.
  - `outline()` adds underline and strikethrough rects from `QFontMetricsF`,
    and applies vertical scale and baseline shift as a transform.
  - `DocumentCodec`, `SvgExporter` (`letter-spacing` in em, `font-weight`,
    `text-transform`, `text-decoration`, `font-kerning`) and `SvgImporter`
    are updated to match.
- **Code:**
  - `src/UI/CharacterSection.{h,cpp}`, placed in `PropertiesPanel` and
    shown by `synchronize()`.
  - `TypeControls` becomes a compact wrapper over the same edit calls.
  - A new `EditorSession::updateText(std::function<void(TextContent&)>,
    name)` so the panel, keys and agent share one path. It replaces the
    loop in `TypeControls::change`.
- **Accept:**
  - With a text object selected under the Selection tool, every field shows
    its value.
  - Changing tracking by +50 widens the outline by 0.05 em per gap (unit
    test on `outline()` width).
  - Leading Auto equals 1.2 × size.
  - Choosing "Bold Italic" from the Style combo on a family that has it
    renders that face.
  - Old `.omai` files with `leading: 1.2, tracking: 2` open looking identical.
  - SVG round-trips keep every new attribute.
  - Every field change is one undo step named after the field ("Tracking",
    "Leading" and so on).

**P0-3. Type keyboard shortcuts (Illustrator defaults)**

> **Done** for tracking (Alt+←/→, ×5 with Ctrl), leading (Alt+↑/↓), baseline
> shift (Alt+Shift+↑/↓), size (Ctrl+Shift+. / ,) and Reset Tracking
> (Ctrl+Alt+Q, which also clears manual kerns). They're Type menu entries,
> remappable under Type in the Keyboard Shortcuts sheet. They work when only
> type is selected or while typing; otherwise Alt+arrows duplicate and nudge,
> as in Illustrator. At a caret with nothing selected, Alt+←/→ kern that pair.
> Held keys make one undo step. Not done: the Ctrl+Shift alignment, All Caps
> and Underline keys, and step sizes as preferences (they're 20/1000 em and
> 2 pt). Tests: `tests/UI/MenusTests.cpp`, `tests/UI/KeyboardShortcutsTests.cpp`.

- **Spec:** these work with text objects selected under the Selection tool and
  while editing inline:
  - **Alt+← / →:** tracking ∓20 (or ∓100 with Ctrl+Alt). When editing with a
    caret and no range, this becomes manual kerning once runs exist (P1); in
    P0 it applies tracking to the object.
  - **Alt+↑ / ↓:** leading ∓2 pt. Alt+↑ reduces leading, as in Illustrator.
  - **Alt+Shift+↑ / ↓:** baseline shift ±2 pt.
  - **Ctrl+Shift+. / ,:** size ±2 pt.
  - **Ctrl+Alt+Q:** reset tracking to 0.
  - **Ctrl+Shift+L / C / R / J / F:** align left, center or right, justify
    left, justify all.
  - **Ctrl+Shift+K:** All Caps.
  - **Ctrl+Shift+U:** Underline.
  - The step sizes are preferences.
  - Holding a key down combines the repeats into one undo step.
- **Code:**
  - `KeyboardShortcuts.cpp` registers the actions so they can be remapped.
  - `EditorCanvas+Keys.cpp` and `InlineTextEditor::claims()` must let Alt+
    arrows through while editing.
  - Handlers go in a new `Menus` Type submenu: Type ▸ Tracking / Leading /
    Size.
- **Accept:**
  - Each shortcut changes the value shown in the Character section.
  - Holding Alt+→ for ten repeats is one undo step.
  - Plain arrows still nudge objects and move the caret.
  - The Keyboard Shortcuts sheet lists them under Type.

**P0-4. Area type (fixed width, wrapping, justify)**

> **Done.** Drag with the Type tool for a box; a click is still point type.
> Justified lines meet both edges exactly. A fixed height hides the rest behind
> a red ⊞ port. Handles and Properties' W/H resize the box and rewrap; the
> Scale tool still scales the glyphs. Type ▸ Convert to Area/Point Type (and
> the context menu and Character's Kind menu) keep every glyph where it was.
> SVG writes a `<tspan>` per line. Tests: `tests/Document/TypographyTests.cpp`,
> `tests/Canvas/EditorCanvasTests.cpp`, `tests/IO/SvgRoundTripTests.cpp`.

- **Spec:**
  - Dragging with the Type tool makes an area text box; clicking still makes
    point text.
  - Text wraps at word boundaries inside the box width, and grows downwards
    (Figma's "Auto height"). An optional fixed height marks overflow with a
    red ⊞ port.
  - Alignment adds Justify (last line left) and Justify all.
  - Type ▸ Convert to Area Type / Point Type.
  - Dragging a side handle of an area text box changes its width without
    scaling the glyphs.
- **Model:** `TextContent` gains `std::optional<QSizeF> area` (nullopt =
  point type). `outline()` switches to `QTextLayout` with
  `QTextOption::WrapAtWordBoundaryOrAnywhere` and justify for area type, and
  keeps `addText` for point type.
- **Code:**
  - `VectorDocument.cpp` (`outline`, bounds).
  - `EditorCanvas+Text.cpp` (drag-to-create).
  - `InlineTextEditor` (caret lines come from the layout).
  - `EditorCanvas+Selection.cpp` (side handles resize the area on text).
  - SVG export of area type writes one `<tspan>` per laid-out line.
- **Accept:**
  - A 200 pt box wraps a long sentence into several lines.
  - Justify-all makes every line 200 pt wide (within 0.5 pt).
  - Resizing the box re-wraps it and leaves the glyph size alone.
  - Converting point → area → point keeps the text.
  - Export matches the canvas.

**P0-5. Precise number fields: math, units, scrubbing, "Mixed"**

> **Done.** `src/UI/NumberField.cpp`. Also: Alt+arrow steps a tenth, and px is
> a point. Tests: `tests/UI/NumberFieldTests.cpp`, `tests/UI/PropertiesPanelTests.cpp`.

- **Spec:**
  - Fields accept arithmetic (`+ - * / ( )`) and `%`, relative to the current
    value.
  - Unit suffixes `pt px mm cm in pc` are converted.
  - A leading operator (`+10`, `*2`) applies per object on multi-selection.
  - Dragging the label scrubs the value; Shift ×10, Alt ×0.1.
  - Empty with the placeholder "Mixed" when values differ.
- **Code:** a `NumberField::evaluate(QString, double current)` parser (a small
  recursive-descent parser; no QJSEngine dependency). The label becomes a
  scrub handle, handled in `NumberField::eventFilter`.
  `PropertiesPanel::moveTo/resizeTo` get a per-object variant.
- **Accept:**
  - Unit tests: `10+5` → 15; `50%` of 200 → 100; `1in` → 72; `2*(3+1)` → 8;
    garbage is rejected and restores the old value.
  - A scrub is one undo step.
  - `+10` on three objects moves each by 10.

**P0-6. Reference point, constrain proportions, Scale Strokes & Corners**

> **Done.** `src/UI/PropertiesPanel+Transform.cpp`,
> `ReferencePointPicker` in `src/UI/PanelSection.cpp`. The reference point
> starts at top left, as Figma's X and Y read, and is remembered. Scale Strokes
> & Effects and Scale Corners both sit in the Transform section's options and
> apply to handle drags, the Scale tool and dialog too (`VectorDocument::transform`,
> `LiveRectangle::transformed`). Off, a live rectangle's radii stay put, clamped to
> half the new rect's shorter side; on, they scale with the average factor
> (`sqrt(|det|)`). Tests: `tests/UI/PropertiesPanelTests.cpp`,
> `tests/Document/LiveCornersTests.cpp`, `tests/Document/TypographyTests.cpp`.

- **Spec:**
  - The Transform section gets a 9-point locator. X/Y show that point, and
    W/H/rotation changes pivot on it.
  - A chain-link toggle keeps W/H proportional.
  - The Transform section's "…" menu has **Scale Strokes & Effects** and
    **Scale Corners** checkboxes, which the Scale dialog and handle drags also
    honour.
- **Code:** `PropertiesPanel::transformSection`, a new `ReferencePointPicker`
  widget in `src/UI/`, and a `QSettings` pref. `EditorSession::scaleSelection`
  gets an origin and a scale-strokes flag (stroke width × sqrt(|sx·sy|)).
- **Accept:**
  - With the centre reference, changing W keeps the centre in place.
  - With the link on, W 100→200 makes H double.
  - Scale Strokes off keeps stroke width through a 200 % scale.

**P0-7. Clipboard and repeat: Paste in Front/Back, Transform Again, Duplicate keys**

> **Done.** Duplicate is Ctrl+Alt+D. Moves, dialogs, drags and Alt-drag or
> Alt+arrow copies are all repeatable, and a scale or rotate repeats about the
> new selection's centre. Tests: `tests/Document/SelectAndRepeatTests.cpp`.

- **Spec:**
  - Ctrl+F pastes in front of the selection and Ctrl+B in back of it, both in
    place.
  - **Ctrl+D is Transform Again:** it repeats the last move, rotate, scale,
    reflect or Alt-drag duplicate, relative to the current selection. That
    gives step-and-repeat: Alt-drag once, then Ctrl+D, Ctrl+D.
  - **Duplicate moves off Ctrl+J** to leave it for **Join** (P1), as in
    Illustrator. Alt+drag and Alt+arrow duplicate. Existing user remaps are
    kept.
- **Code:**
  - `EditorSession` stores `m_lastTransform` (a `QTransform` plus a
    "duplicate" flag), set in `transformSelection`, `commitInteraction`
    (move/scale/rotate drags) and the ObjectDialogs.
  - New `transformAgain()`, and `paste(PastePosition)` with the values
    {center, inPlace, front, back}.
  - `Menus.cpp` gets the Edit and Object ▸ Transform entries.
- **Accept:**
  - Alt-drag a copy 50 pt right, then press Ctrl+D twice: there are three
    copies spaced 50 pt apart.
  - Rotate 15° then Ctrl+D rotates another 15°.
  - Paste in Back puts the objects directly below the selection in z-order
    within its parent.
  - Ctrl+F while editing text doesn't paste.

**P0-8. A Select menu**

> **Done**, plus Reselect (Ctrl+6), Open Paths and All on Same Layers.
> Tests: `tests/Document/SelectAndRepeatTests.cpp`, `tests/UI/ContextMenusTests.cpp`.

- **Spec:**
  - A new **Select** menu between Object and Type (Illustrator order):
    - All (Ctrl+A), Deselect (Ctrl+Shift+A), **Inverse**.
    - **Next Object Above** (Ctrl+Alt+]), **Next Object Below** (Ctrl+Alt+[).
    - **Same ▸** Fill Color, Stroke Color, Fill & Stroke, Stroke Weight,
      Opacity, Blend Mode, Font Family, Font Family & Style & Size.
    - **Object ▸** All Text Objects, Clipping Masks, Images, Stray Points.
  - Select Same compares against the first selected object and searches the
    whole document, leaving out hidden and locked objects.
- **Code:** `Menus::buildSelect`, `EditorSession::selectSame(SameKey)`,
  `selectInverse()`, `selectAdjacent(bool above)`. The matching logic goes in
  `VectorDocument` so the agent bridge can reuse it.
- **Accept:**
  - Select Same Fill Color on a red shape selects every red-filled visible,
    unlocked object and nothing else.
  - Inverse of one object in a layer of five selects the other four.
  - Stray Points finds single-anchor paths.

**P0-9. Measuring and zoom**

> **Done.** Lines and labels use the theme's accent, and rotate drags read the
> angle. Tests: `tests/Canvas/CanvasQuickKeysTests.cpp`.

- **Spec:**
  - **Hold Alt** with a selection and hover another object (or the artboard).
    Red distance lines and pt labels appear between the nearest edges, H and V,
    as in Figma and Paper.
  - Smart guides also show **Δx/Δy and W×H labels** during move and scale
    drags (Illustrator's measurement labels).
  - **Zoom to Selection** (Shift+2 and Ctrl+Alt+0 in the View menu) and
    **Fit Artboard** (Shift+1 as an alias).
- **Code:** `EditorCanvas+Paint.cpp` draws the overlays, and
  `EditorCanvas+Cursors.cpp` handles hover with Alt.
  `EditorSession::zoomToSelection()` goes next to `zoomToFit` and works
  through `CanvasViewport`.
- **Accept:**
  - With object A selected and B 30 pt to its right, Alt+hover over B shows
    one horizontal line labelled "30".
  - Zoom to Selection frames the bounds with a margin.
  - The labels read in the theme's colours in light and dark themes.

**P0-10. Nudge preferences and keyboard opacity**

> **Done.** Edit ▸ Preferences… holds the keyboard increment; Shift is always
> ten times it, so there's no separate big-step field. Tests:
> `tests/Canvas/CanvasQuickKeysTests.cpp`.

- **Spec:**
  - Preferences hold a Keyboard Increment (default 1 pt) and a big step
    (default 10 pt).
  - **Alt+arrow** duplicates, then nudges.
  - With the Selection tool active and no field focused, number keys set
    opacity: 1–9 → 10–90 %, 0 → 100 %, and two digits in quick succession
    set an exact value (Figma and Paper).
- **Code:** `EditorCanvas+Keys.cpp` (step from `QSettings`), a Preferences
  sheet (`src/UI/PreferencesSheet.{h,cpp}`, in the Edit menu), and
  `EditorSession::setOpacityOfSelection`.
- **Accept:**
  - Changing the increment to 0.5 moves the selection by 0.5 pt per arrow
    press.
  - Alt+→ leaves the original in place and moves a copy.
  - Typing "5" sets 50 %, and "0" then "5" within 500 ms sets 5 %.
  - Digits typed into a field or inline text do nothing to opacity.

### P1: the detail layer

> **P1-7, P1-8, P1-12, P1-13 and P1-14, P1-17 done.**
>
> **Appearance stack (P1-7):** `VectorObject` keeps `fill` and `stroke` as the
> bottom of each stack, with `extraFills` and `extraStrokes` above them
> (`fills()` and `strokes()` read the whole stack). Each `Paint` carries its
> own eye, opacity and blend mode. Old files read unchanged: the stack is two
> optional keys, `moreFills` and `moreStrokes`, and the new paint and stroke
> fields are written only when set. In Properties, one plain fill and one plain
> stroke stay as the familiar rows, each with a **+**; a second entry, or one
> with its own opacity, blend or eye, turns the row into a Figma-style stack:
> the top entry first, each row with a grip to drag, an eye, a well, its hex,
> opacity (fills) or weight (strokes) and **−**; right-click a row for its
> blend mode. Clicking a stroke row makes it the one the Stroke section edits.
> Fills draw under strokes, in order. SVG writes a stacked object as one `<g>`
> named for it, with one element per visible entry; PDF draws through the
> renderer. Outline Stroke makes one filled path per visible stroke, and
> Pathfinder's result takes the top object's whole stack. Every change is one
> named step (Add Fill, Remove Stroke, Reorder Fills, Hide Fill…).
>
> **Stroke align and arrowheads (P1-8):** `StrokeStyle` gains `alignment`,
> `startArrow`, `endArrow`, `arrowScale` and `alignDashes`, and
> `Document/StrokeGeometry` works out what they cover, so the renderer, bounds,
> SVG and Outline Stroke agree. Inside and outside draw a double-width stroke
> clipped to one side of the path, and apply to closed paths only. Heads are
> arrow (an open chevron), triangle, circle, square and bar, sized from the
> weight times the scale; a triangle trims the line so it doesn't poke through
> the tip. Align to corners stretches the dash pattern per run so a whole dash
> sits centred on every corner and path end. In Properties the Align menu shows
> only when the selection has a closed path, the Arrows row only when it has
> an open one, the scale only once a head is set, and Align to corners only
> once there are dashes. SVG writes such strokes as their filled outline. The
> agent's `set_style` takes `align`, `startArrow`, `endArrow` and `arrowScale`.
>
> **Colour (P1-12):** a hex field in the fill and stroke rows and in each stack
> row (`ff6600`, `#FF6600` and `#f60` all work). The picker keeps the last 12
> colours chosen anywhere in the app (QSettings `colors/recent`) as a strip under
> it. **Selection colors** in Appearance lists every colour of a selection of two
> or more painted objects, gradient stops and styled text runs' own colours
> included; picking a new one for a chip is one "Recolor" step across the
> selection, runs and all (`EditorSession::replaceColor`).
> **Global swatches:** right-click a swatch for **Global** and **Edit Color…**. A
> global swatch shows Illustrator's white corner; applying it links the paint by
> the swatch's id (`Paint::swatchId`), and editing its colour recolours every
> linked paint in every open document, one "Edit Swatch" step each. Changing a
> linked colour by hand unlinks it. The library stays per install, as before.
>
> **Copy/Paste Properties (P1-13):** Edit ▸ Copy Properties (Ctrl+Alt+C) and
> Paste Properties (Ctrl+Alt+V), also in the right-click menu and Ctrl+K. They
> carry the whole stack, opacity and blend; between texts, the character style
> too (not the words, box or kerning). Text onto a path, or a path onto text,
> is paint only. One clipboard serves every tab. The eyedropper's **Alt-click**
> gives the selection's style (or the defaults) to the object clicked.
>
> Tests: `tests/Document/PaintAppearanceTests.cpp`, `tests/IO/SvgAppearanceTests.cpp`,
> `tests/UI/PaintStackTests.cpp`, `tests/Canvas/GradientToolTests.cpp`.
>
> **Command palette:** `src/UI/CommandPalette*.cpp`. Ctrl+K and Ctrl+/ (the
> first remappable as "Command Palette"), and Help ▸ Command Palette…. It is a
> Figma-style floating box over the window, keyboard-first (arrows, Enter,
> Esc; a click elsewhere puts it away). It reaches:
> - every menu bar entry, with its menu path and current key
> - the selection's right-click entries (Align, Pathfinder, Isolate)
> - every tool with its key
> - panels and recent files
> - document settings (Artboard Size, Show Grid, Snap to Grid, Smart Guides, Keyboard Increment)
> - AI actions, including Roast My Design at the saved heat
>
> Matching runs whole name, prefix, every word's start, substring, initials
> ("co" is Create Outlines), then letters in order; menu paths and keywords
> ("document setup", "nudge") find entries too. The last eight commands come
> first. Unavailable entries show dimmed, sink below the rest, and say so
> instead of running. When the text starts with "?", or reads as a request (a
> sentence, or a few words no command answers to), the top row is "Ask
> <agent>: …". It runs Edit with Instruction on the selection, or on the
> document when nothing is selected. With nothing selected it runs Generate
> instead when the text asks for new art ("draw…", "a fox logo") or the
> artboard is empty. Everything the agent does arrives as a proposal to keep
> or discard. A failed start stays open with the reason. Tests:
> `tests/UI/CommandPaletteTests.cpp`.
>
> **Contextual task bar:** `src/Canvas/TaskBar.cpp` (the widget: place, hide,
> fade, grip) and `src/UI/TaskBarActions.cpp` (what it holds, from the menu
> bar's own actions). What each selection gets:
> - one path: fill, stroke, Edit Path (Direct Selection) and Path ▸
> - several paths: fill, stroke, Pathfinder ▸, Shape Builder, Align ▸ and Group
> - mixed objects: Align ▸, Group, and Pathfinder ▸ when they combine
> - type: font, size, Create Outlines and Area/Point Type
> - an image: Image Trace and Vectorize with AI
> - a group: Ungroup, Isolate, fill and stroke
> - a clipping group: Release Clipping Mask and Ungroup
>
> Every bar ends with an **Ask AI…** field (Enter runs Edit with Instruction
> on the selection) and ⋯, which opens the selection's right-click menu. It
> sits 26 px under the selection's bounds, clear of the rotate zone. With no
> room below it goes above, with no room either way over the view's bottom,
> and it always stays inside the view. It hides during drags, pen paths,
> typing on the canvas, an agent's edit and its proposal. Near a selection
> handle it fades to 15 % and lets clicks through. Dragging the grip moves it
> and remembers the offset; the grip's right-click pins it in place, resets
> it, or hides it. View ▸ Contextual Task Bar turns it on and off, remembered.
> Tests: `tests/Canvas/TaskBarTests.cpp`, `tests/UI/TaskBarActionsTests.cpp`.

> **P1-6, P1-9, P1-10, P1-11, P1-15 and P1-16 done.**
>
> **Rulers and guides (P1-6):** `src/Canvas/Rulers.cpp` (the strips, drawn
> over the canvas's top and left edges; clicks pass through) and
> `EditorCanvas+Guides.cpp`. View ▸ Rulers (Ctrl+R) shows them, in points,
> with the pointer marked on each. Dragging out of the top ruler makes a
> horizontal guide, out of the left a vertical one (Shift or Snap to Pixel
> keeps it on whole points); dragging a guide moves it, back onto its ruler
> removes it, and a double-click types its place. Guides are
> `VectorDocument::guides`, saved in `.omai`, every change one undo step (Add,
> Move, Delete, Clear Guides). They snap bounds and drawn points through
> `SmartGuides::addGuides`, whether or not smart guides are on, while they
> show. View ▸ Guides: Hide (Ctrl+;), Lock (Ctrl+Alt+;; locked guides can't be
> dragged and draw quieter), Make Guides (Ctrl+5: a straight line becomes its
> guide, any other path its bounds' four edges), Release Guides (Ctrl+Alt+5:
> each guide back into a line across the artboard) and Clear Guides. Hidden
> and locked are view state, like the grid. Only straight guides: Illustrator's
> path-shaped guides are out.
>
> **Live corners (P1-9):** `VectorObject::shape` holds a `LiveRectangle`
> (`src/Document/LiveRectangle.cpp`: its own rect, a rigid placement, four
> radii and four styles). The Rectangle and Rounded Rectangle tools draw them.
> Moving, rotating, reflecting and uniform or upright scaling keep it live
> (radii scale with it); a skew makes it a path. It's live only while the path
> is still what it makes, so editing any anchor expands it for good
> (`expandEditedShapes`, on every commit), and only a live shape is saved. A
> Shape section in Properties shows one Radius field, a link that splits it
> into four (TL, TR, BR, BL; uneven corners always show four) and a ⋯ menu for
> Round, Inverted Round and Chamfer. With Direct Selection each corner has a
> widget: drag it for every selected corner's radius, Alt-drag for that corner
> alone, Alt-click to cycle its style. Scale Corners (P0-6) honours the radii:
> off, they stay put (clamped to the new rect); on, they scale with it.
>
> **Isolation (P1-10):** `EditorSession::isolation()` is the stack of groups
> entered (outermost first), so undo, the canvas and the panels share it. The
> canvas draws everything else at 50 % and the group on top at full; clicks,
> marquees and Select All reach only the group's children; new objects and
> pastes go into it with any tool. `src/UI/IsolationBar.cpp` sits above the
> canvas: a back arrow (one level) and crumbs from the layer down, each
> stepping out to its level. Esc leaves; a double-click outside steps out a
> level. A single click outside now only deselects, as in Illustrator.
>
> **Path editing (P1-11):** `EditorSession+Paths.cpp`. Object ▸ Path ▸ Join
> (Ctrl+J): with Direct Selection two picked end anchors; otherwise one open
> path closes, or several join nearest end to nearest end. Ends closer than
> 0.01 pt merge into one anchor, others meet with a straight segment. Average…
> (Ctrl+Alt+J) asks Horizontal, Vertical or Both and moves the picked anchors,
> or every selected anchor. The Scissors tool (C, beside Shape Builder) cuts at
> an anchor or where a segment is clicked: a closed contour opens with both
> ends there, an open one splits into two paths. Reverse Path Direction turns
> the selected paths (or the contours with picked anchors). The fill rule
> shows in the Shape section for compound paths, and as Object ▸ Compound Path
> ▸ Even-Odd Fill Rule; SVG already read and wrote `fill-rule`.
>
> **History (P1-15):** `src/UI/HistoryPanel.cpp`, from Window ▸ History, a
> floating panel following the front document. Rows are "Open" then every step
> by name; steps ahead of the current one show dimmed and italic, and a click
> goes to just after that row (`EditorSession::stepHistory`). Preferences sets
> History states (1–1000, default 100), read by every document at its next
> edit.
>
> **Key object and spacing (P1-16):** a click (no drag) on one of several
> selected objects makes it the key object, drawn with a 3 pt box; clicking it
> again, or any new selection, clears it. Align To gains "To key object", which
> Properties switches to when one is clicked; the key never moves. The Align
> section has six distribute buttons (left, centre, right, top, middle,
> bottom) and a spacing row: horizontal and vertical Distribute Spacing and a
> Gap field. With a gap, the key object (else the first) holds still and the
> rest step out from it; empty reads Auto and spaces evenly between the
> outermost two.

> **P1-1 to P1-5 done: the type detail layer.**
>
> **Styled runs (P1-1):** `TextContent` is now its own `CharacterFormat` and
> `ParagraphFormat` (the object's look), plus `runs` (character ranges
> formatted apart, each a full format: family, style, size, tracking, baseline
> shift, case, decoration, OpenType features, colour) and `paragraphFormats`
> (paragraphs formatted apart, by index). It stays one string with ranges on
> it rather than nested paragraph objects, so everything that reads `text.size`
> or `text.family` still reads the object's own format. `replace()` keeps runs,
> kerns and paragraphs on their characters as text is typed or deleted; typed
> text takes the format it replaces, else the one before it.
> - While type is edited in place, the canvas hands its selection to
>   `EditorSession::setTextRange`. With characters selected, every type edit
>   (the Character fields, the type keys, styles) and a solid fill colour apply
>   to them alone; with only a caret, to the whole object, as before. Each
>   stretch of the target is edited on its own, so a size step keeps each run's
>   size and an absolute value reaches every run. The Character section reads
>   Mixed when the stretches differ.
> - Layout: runs become `QTextLayout` formats. Sizes that miss the object's
>   pixel grid are laid out on a 1/16 pt grid, so a 13.5 pt run is exact. Auto
>   leading is 120 % of the largest size on each line.
> - Manual kerning is still per caret (Alt+←/→ with no range) and shifts only
>   what follows.
> - Create Outlines makes one path, or a group of one path per run colour.
> - `.omai` is version 3: runs and paragraphs are written as differences from
>   the object's format, so version 2 files read unchanged. SVG writes a
>   `<tspan>` per run inside each line's span and reads nested spans back as
>   runs (the most common look becomes the object's).
> - Deviations: horizontal and vertical scale and the kerning mode stay per
>   object, since SVG can't scale one span. A manual kern still separates its
>   character's shaping, so a kern pair (AV) around a manual kern is lost, as
>   it was in P0.
>
> **OpenType (P1-2):** `Document/FontFeatures`, `UI/OpenTypePopover`. A "…"
> button at the end of Show more's decoration row opens a popover: Standard
> and Discretionary ligatures, Contextual alternates, Small caps, Fractions,
> Ordinals, Figures (Lining or Oldstyle) and Spacing (Tabular or Proportional),
> and stylistic sets 1–20. The font's GSUB and GPOS feature lists decide what's
> enabled. Features apply through `QFont::setFeature` on Qt 6.7 and later; on
> older Qt the button is left out and the features are only kept. SVG writes
> `font-feature-settings`.
>
> **Paragraph (P1-3):** `UI/ParagraphSection`. Shown when every selected text
> is area type: left and right indent, first-line indent (below 0 hangs),
> space before and after, and, for justified text, where the last line sits
> (left, center, right: `justifyCenter` and `justifyRight`; Justify all stays
> in Character). Each paragraph can differ; with characters selected, fields
> apply to the paragraphs they touch.
>
> **Text styles (P1-4):** `EditorSession+TextStyles.cpp`, `UI/TextStylesPanel`,
> `CharacterSection+Styles.cpp`. `VectorDocument::textStyles` holds character
> styles (character attributes) and paragraph styles (paragraph attributes
> plus the character attributes their unstyled characters take). Colour isn't
> part of a style, as in Figma. The style ids live in the formats themselves
> (`CharacterFormat::characterStyle`, `ParagraphFormat::paragraphStyle`)
> rather than a `TextContent::styleId`, so a run can carry its own.
> - Character's heading has a style button: it names the style shown, with
>   "+" when overridden, and its menu applies styles, makes New Paragraph or
>   New Character Style from the selection, and offers Redefine, Clear
>   Overrides, Detach Style and Type Styles….
> - Window ▸ Type Styles lists both kinds. A click applies, a double-click
>   renames, the right-click menu redefines or deletes, and "+" makes one.
> - Redefine updates every use in one undo step. Fields someone changed by
>   hand keep their values, as Illustrator's overrides do.
> - Deleting a style keeps the look of the text that used it.
>
> **Find Font (P1-5):** `UI/ObjectDialogs+Fonts.cpp`,
> `EditorSession::replaceFont`. Type ▸ Find/Replace Font… lists the families
> used in the document or the selection, missing ones marked with a warning.
> **Find** selects the text that uses one. **Replace All** swaps it everywhere
> (runs and styles too) for the chosen family, at the nearest face, in one undo
> step. Opening a file with missing fonts says so in the status line instead of
> an alert.
>
> Tests: `tests/Document/TextRunsTests.cpp`, `tests/Document/TextStylesTests.cpp`,
> `tests/IO/TextRunsIOTests.cpp`, `tests/Canvas/TextRangeTests.cpp`,
> `tests/UI/TypePanelsTests.cpp`.

| # | Item | One-line spec | Where | Acceptance |
|---|---|---|---|---|
| P1-1 | **Styled text runs** | `TextContent` becomes `paragraphs → runs` (a style per run: family, style, size, tracking, baseline shift, case, decoration, fill). Selecting a range in `InlineTextEditor` and changing a field styles only that range. **Manual kerning** is a per-caret pair value (Alt+←/→ at a caret with no range). | `VectorDocument.h`, `InlineTextEditor`, `DocumentCodec` (with migration), `SvgExporter` (`<tspan>`), `CharacterSection` | Bold on one word of a sentence round-trips through `.omai` and SVG; kerning between two letters shifts only the letters after it; Create Outlines keeps per-run styling |
| P1-2 | **OpenType features** | A "…" popover with Ligatures, Discretionary ligatures, Contextual alternates, Small caps (true `smcp`), Fractions, Ordinals, Tabular/Proportional and Lining/Oldstyle figures, Stylistic sets 1–20. Features the font lacks are disabled. | `CharacterSection`, `TextContent::font()` via `QFont::setFeature` (**Qt ≥ 6.7**; hide it on older Qt behind `#if QT_VERSION`) | Turning `liga` off on "office" in a font with ligatures changes the glyph count of `outline()`; the controls are hidden on Qt 6.4 builds |
| P1-3 | **Paragraph settings** | Left, right and first-line indents, space before and after, and the justify variants. With area type, a Paragraph section appears in Properties. | `TextContent` (per paragraph once P1-1 lands), `QTextLayout` in `outline()` | A −12 pt first-line indent hangs the first line; space after 6 pt adds 6 pt between paragraphs |
| P1-4 | **Text styles** | Character and paragraph styles stored in the document: New from selection, apply, redefine, and a "+" on the style name when overridden. Shown in a Styles list in the Character section, and as a Window ▸ Type Styles panel. | `VectorDocument` (`std::vector<TextStyle>`), `TextContent::styleId`, `DocumentCodec`, new `src/UI/TextStylesPanel` | Redefining a style updates every text that uses it in one undo step; a local override shows "+" |
| P1-5 | **Find Font** | Type ▸ Find/Replace Font… lists the families used in the document, marks missing ones, and replaces them across the document or the selection. | `ObjectDialogs` (new dialog), `EditorSession::replaceFont` | Opening a file with a missing font lists it with a warning; Replace All changes every use in one undo step |
| P1-6 | **Rulers and guides** | Ctrl+R shows rulers (pt/px/mm by document units) with pointer markers. Drag out a guide; guides snap, lock (Alt+Ctrl+;), hide (Ctrl+;), clear, and are made from a path (Ctrl+5). Double-click a guide to type its position. | `VectorDocument::guides`, `EditorCanvas+Paint.cpp`, new `src/Canvas/Rulers.{h,cpp}` (widgets beside the canvas), `SmartGuides` targets | Guides save in `.omai`; a moved object snaps to a guide within the tolerance; locked guides can't be dragged |
| P1-7 ✓ | **Multiple fills and strokes (Appearance stack)** | Replace `Paint fill; StrokeStyle stroke` with ordered lists, each with visibility, opacity and blend mode. Properties shows a stack with + / − / eye / drag-to-reorder. The single-fill UI stays as the collapsed view. | `VectorDocument.h` (migrate single to list), `VectorRenderer`, `PropertiesPanel+Appearance.cpp`, SVG export (duplicated `<path>`s) | Two strokes (a thick dark one under a thin light one) render and export as one object; old files open unchanged |
| P1-8 ✓ | **Stroke align and arrowheads** | Add `alignment` to `StrokeStyle` (inside and outside draw as a clipped double-width stroke), and start/end arrowheads (arrow, triangle, circle, square, bar) with a scale %. Add dash "align to corners". | `Paint.h`, `VectorRenderer`, `PathOperations` (outline stroke honours both), `PropertiesPanel+Appearance.cpp` | An inside 10 pt stroke on a 100 pt square stays within its bounds; Outline Stroke on an arrowed line includes the head |
| P1-9 | **Corner radius per corner / live corners** | Rectangles keep `cornerRadii[4]` while they're still live shapes. Properties shows one radius field, or four when unlinked. With Direct Selection, a corner widget drag sets the radius, and Alt-click cycles round, inverted and chamfer. | `VectorObject` (a `shape` block: kind, rect, radii), `PathOperations`, `EditorCanvas+DirectSelection.cpp` | Changing one corner leaves the other three alone; editing any anchor converts the object to a plain path (Illustrator's "expand shape") |
| P1-10 | **Isolation mode** | Double-click a group, or use the context menu, to edit inside it. Everything else dims to 50 % and can't be selected. A breadcrumb bar appears under the tab bar; Esc or double-clicking outside leaves. | `EditorSession::isolated` (an id stack), `EditorCanvas+Selection.cpp`, `VectorRenderer` dim pass, `ProjectWorkspaceView` breadcrumb | Clicks select children of the isolated group only; new objects go into it; Esc restores the normal view |
| P1-11 | **Join, Average, Scissors, Reverse Path, fill rule** | Ctrl+J joins two picked endpoints (with a straight segment, or merged if they coincide). Alt+Ctrl+J averages anchors H, V or both. A Scissors tool (C) splits a path at a click. Object ▸ Path ▸ Reverse Direction. A fill rule toggle (non-zero / even-odd) in Properties for compound paths. | `PathOperations`, `EditorSession`, new `Tool::scissors`, `EditorCanvas+DirectSelection.cpp` | Joining two open paths gives one path; Scissors on a closed path gives one open path whose ends are at the click; the fill rule round-trips through SVG |
| P1-12 ✓ | **Colour QoL** | A hex field in the fill/stroke row; a **recent colours** strip (the last 12, per app) in the picker; **Selection colors** listing the distinct colours in a mixed selection, each editable (an edit recolours every use); **global swatches** (a swatch reference kept on the paint, so editing the swatch updates every use). | `ColorPickerSheet`, `PropertiesPanel+Appearance.cpp`, `Swatches` (+ `Paint::swatchId`), `EditorSession::replaceColor` | Typing `#ff6600` applies it; recolouring one colour in Selection colors changes it on all selected objects in one undo step; editing a global swatch updates every use |
| P1-13 ✓ | **Copy/Paste properties** | Ctrl+Alt+C copies the style (fills, strokes, opacity, blend, text style); Ctrl+Alt+V applies it to the selection. The eyedropper's Alt-click applies to the clicked object. | `EditorSession` (a style clipboard; reuses `pickStyle`), `Menus`, the context menu | Pasting properties from text onto a path applies only paint; from text onto text also applies the character style |
| P1-14 | **Command palette** | Ctrl+K (and Ctrl+/) opens a search sheet over every named `QAction`, showing the menu path and shortcut, enabled state, and recent commands first. Enter runs; arrows move. | New `src/UI/CommandPalette.{h,cpp}`, built from `Menus`' actions; theme from `OmarchyTheme` | Typing "outl" finds Outline Stroke, Create Outlines and Outline mode; disabled actions show dimmed and don't run |
| P1-15 | **History panel** | Window ▸ History lists undo step names; clicking a row undoes or redoes to it. The limit is a preference. | `DocumentHistory` (exposes names), new `src/UI/HistoryPanel` | Clicking three rows up equals three Ctrl+Z; a new edit after that drops the redo rows |
| P1-16 | **Distribute spacing and key object** | A click on an already-selected object (Selection tool) marks it as the key object (thick outline). Align To gains "Key Object". Distribute spacing takes a value in pt, and the four edge-distribute buttons are added. | `EditorSession` (`m_keyObject`), `PropertiesPanel::alignSection`, `EditorCanvas+Selection.cpp` | With the key object set, Align Left moves the others to its left edge and not the key; distribute spacing 12 puts exactly 12 pt between neighbours |
| P1-17 | **Contextual task bar** | A small floating bar under the selection with 3–5 actions for its kind: path → Edit Path, Outline Stroke, Lock; several objects → Group, Align, Unite; group → Ungroup, Isolate; text → Area/Point, Create Outlines, font size; image → Image Trace, Vectorize with AI. It can be dismissed, and View ▸ Contextual Task Bar toggles it. | New `src/Canvas/TaskBar.{h,cpp}` as a child of `EditorCanvas`, positioned from `selectionBounds` | It follows the selection during pan and zoom, hides during drags and text editing, and stays hidden across restarts once turned off |
| P1-18 | **Properties panel structure** | Collapsible sections that remember their state; sections that depend on the selection (Character, Paragraph, Image, Group); a real splitter with Layers; and a "Mixed" state everywhere. See section 7. | `PropertiesPanel.cpp`, `ContentView+Panels.cpp` | Collapsing Stroke survives a restart; selecting an image shows the Image section (Trace, Vectorize) and no Stroke |

### P2: bigger features, later

> **P2-11 done.** View ▸ Snap to Pixel rounds drawn points, handle drags,
> moved bounds' top left, corner radii and guides to whole points on whatever
> axis a guide hasn't already taken. View ▸ Pixel Grid (on by default) draws a
> line per point once the zoom reaches 600 %. Object ▸ Make Pixel Perfect
> (`EditorSession::makePixelPerfect`, also in the context menu for paths) snaps
> the selection's anchors to whole points in one step, handles moving with
> their anchor; a straight, axis-aligned edge lands on the grid since both ends
> round the same shared coordinate. An upright live rectangle snaps its rect's
> edges directly and stays live; any other shape whose anchors move loses its
> live shape, as an edited anchor does. Tests: `tests/Document/PathEditingTests.cpp`.

| # | Item | One-line spec | Where | Acceptance |
|---|---|---|---|---|
| P2-1 | **Multiple artboards** | `VectorDocument::artboards` (name, rect, background), the Artboard tool (Shift+O) with the documented context menu (New, Duplicate, Rename, Delete, Fit to Artwork Bounds, Switch Orientation), an Artboards list, and export per artboard. | `VectorDocument`, `EditorSession`, a new tool, `ExportSheet`, `DocumentExporter` | Two artboards export to two PNGs named after them; Fit to Artwork Bounds hugs the art |
| P2-2 | **Shape Builder** (Shift+M) | Drag across the regions of overlapping selected shapes to merge them; Alt-drag removes them. The regions are computed from a planar arrangement of the outlines. | New `PathOperations::regions()`, a new tool in `EditorCanvas` | Merging two regions of three overlapping circles gives one path plus the rest untouched; one undo step |
| P2-3 | **Type on a path** | Type on a Path tool: glyphs placed along a path with `QPainterPath::pointAtPercent`/`angleAtPercent`, a start bracket to slide, and flip. | `TextContent::onPath`, `VectorDocument::outline` | Outlines follow a circle; the text stays editable; SVG export uses `<textPath>` |
| P2-4 | **Text wrap and threads** | Area type flows around objects marked "text wrap" (with an offset), and threads overflow into a linked box. | `TextContent`, the layout in `VectorDocument.cpp` | Text avoids a wrap object's bounds + offset; overflow continues in the next box |
| P2-5 | **Hyphenation** | Optional, using hyphen patterns (TeX patterns, `hyphen` data) for area type. Honour soft hyphens (U+00AD) first. | The area-type layout | Soft hyphens break with a visible hyphen at a line end and are hidden otherwise |
| P2-6 ✓ | **Symbols** (done as components: docs/DESIGN-SYSTEMS.md) | A symbol definition stored in the document plus instances (a transform plus an id). Editing the master updates every instance; Break Link expands one. | `VectorDocument`, `VectorRenderer`, a Symbols panel | Recolouring a master updates 10 instances in one undo step |
| P2-7 | **Width tool / variable strokes** | Width points along a stroke, and profiles, rendered as an outline. | `StrokeStyle::widthProfile`, `PathOperations` | Uniform, tapered and bulge profiles render and export as filled outlines |
| P2-8 ✓ | **On-canvas gradient annotator** (done: see below) | With the Gradient tool (G), drag the start and end on the object and drag the stops. | `EditorCanvas`, `Paint` | Dragging the end point changes the gradient angle live; one undo step |
| P2-9 | **Opacity masks** | Make Mask (top object's luminance), Clip, Invert. | `VectorObject::mask`, `VectorRenderer` | A white-to-black gradient mask fades the art; PDF and SVG export keep it |
| P2-10 | **Export for Screens / per-object export** | Mark objects or artboards for export; scales 0.5×–3× with suffixes; PNG/JPG/SVG/PDF/WebP in one batch. | `ExportSheet`, `DocumentExporter` | Exporting two assets at 1× and 2× writes four files with `@2x` suffixes |
| P2-11 ✓ | **Snap to pixel / Make Pixel Perfect** | Snap anchors and bounds to whole pixels while drawing and moving; pixel grid at ≥ 600 %. Object ▸ Make Pixel Perfect snaps the selection onto the grid after the fact, one undo step. | `EditorSession::snapped`, `EditorCanvas+Paint.cpp`, `EditorSession::makePixelPerfect` | A dragged rectangle's edges land on integers; Make Pixel Perfect on a path with fractional anchors rounds them to whole points |
| P2-12 | **Toolbar flyouts and presets** | Group tools with flyouts, Alt-click to cycle, and Basic/Advanced presets. | `ContentView`, `ToolIcons` | The tool order is saved; flyouts open on long-press or right-click |

> **P2-8 done.** `src/Canvas/EditorCanvas+Gradient.cpp`. The Gradient tool
> (G, in the toolbar by the eyedropper) edits the first selected object's fill.
> A drag across it sets the gradient from where it started to where it ended
> (a solid becomes a gradient from its colour to white); Shift keeps the bar to
> 45° steps. The bar shows Illustrator's circle at the origin and square at the
> end, with each stop as a swatch beside it: drag an end to change the angle and
> length, or a stop to slide it along. Everything previews live and ends in one
> "Gradient" step. A click on another object selects it. Radial gradients use
> the same bar: centre and radius.

### Out of scope

Figma-style auto layout, components with variants and multiplayer or cloud
features don't fit an offline illustration tool. Paper's HTML-native model
belongs with Omastrator's Live feature, not the core document.

### Suggested order

1. **Pass 1:** P0-1 (context menus) and P0-7 (Paste Front/Back, Transform
   Again, Duplicate off Ctrl+J). They're small and change how the app feels
   straight away.
2. **Pass 2:** P0-2 and P0-3 (the Character section and type keys). This
   carries the model migration for tracking, leading and style.
3. **Pass 3:** P0-5 and P0-6 (number fields and the reference point), then
   P0-8 (the Select menu).
4. **Pass 4:** P0-9 and P0-10 (measuring, zoom to selection, nudge prefs,
   opacity keys), then P0-4 (area type).
5. **After that,** in P1 order: P1-18 (the panel structure, which gives
   everything else somewhere to live), P1-1 and P1-2 (runs and OpenType),
   P1-6 (rulers and guides), P1-7 and P1-8 (the appearance stack and
   strokes), and P1-14 (the command palette).
