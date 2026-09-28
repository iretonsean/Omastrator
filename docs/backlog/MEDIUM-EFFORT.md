# Medium effort (2026-09-28)

Features that take an agent a few days each. They touch the document model
or the renderer, or add a panel. The order is roughly value for effort.
Demand figures come from docs/research/unbuilt-features-2026-09.md and the
gaps from docs/FIGMA-AUDIT.md.

## 1. Spell check in type
- **Demand:** Illustrator's Grammarly request, 432 votes, in Adobe's
  backlog.
- **Do:**
  - Underline misspellings in the inline text editor through Hunspell
    (LGPL/MPL, optional at build time) and the system dictionaries.
  - Add suggestions in the right-click menu, and Add to Dictionary.
  - Add "Check Spelling with AI" on the selection, through the agent, as a
    preview.

## 2. Pages
- **Demand:** it's missing (docs/FIGMA-AUDIT.md), and it's the first phase
  of docs/BROWSER-FRAMES.md.
- **Do:**
  - Several canvases per document, in the model and the `.omai` (an
    additive key).
  - A Pages list at the top of Layers, Ctrl+K entries, and Next and
    Previous page.
  - Export and Share follow the active page.

## 3. Image fills
- **Demand:** it's missing (docs/FIGMA-AUDIT.md: `PaintKind` has no image).
  Figma, Sketch and Penpot imports need it.
- **Do:**
  - An image paint with Fill, Fit, Crop and Tile modes, and exposure and
    contrast where cheap.
  - The paint stack UI, the codec, and SVG export (as a pattern) and import.

## 4. Live boolean groups
- **Demand:** it's partial (Pathfinder is destructive), and Figma and Sketch
  imports produce live booleans.
- **Do:** a group kind whose children stay editable and whose result is
  worked out at render time through `PathOperations`, with Flatten to make
  it permanent.

## 5. Grid auto layout with Hug/Fill
- **Demand:** Figma (2/5, but CSS grid is everywhere). Lift and the Penpot
  import meet grid layouts.
- **Do:** a Grid direction beside Row and Column, with tracks, gaps and
  Hug/Fill/Fixed per track, reusing `src/Document/AutoLayout.cpp`.

## 6. Auto layout and frame leftovers (docs/FIGMA-AUDIT.md)
- Drag children into and out of frames.
- Drag to reorder inside auto layout.
- Min and max sizes, and baseline alignment.
- Canvas handles for padding and gap.

## 7. Layout grids on frames
- Column, row and square grids, shown on the canvas, snapped to, and
  stored per frame.

## 8. Corner smoothing
- Figma-style squircle corners on live rectangles and frames. A smoothing
  value in the corner controls, with SVG export as paths.

## 9. Colour and effect styles as library objects
- Named colour and effect styles, distinct from tokens, in the Design
  System panel. Applying one binds it, and editing it updates every use.
- Effect styles wait for effects (HIGH-EFFORT.md).

## 10. Per-side stroke weights
- Separate top, right, bottom and left stroke weights on frames and
  rectangles, as Figma has.

## 11. Vector halftones
- **Demand:** Illustrator, 200 votes. Only a paid plugin does it today.
- **Do:** Object ▸ Create Halftone… turns an image or gradient into editable
  vector dots or lines (size by luminance, angle, cell size, shape), as one
  undo step. A live version can follow effects.

## 12. Basic chart generator
- **Demand:** Illustrator, 342 votes ("improve the graph tool").
- **Do:** Object ▸ Chart… makes bar, line and pie charts from pasted or CSV
  data as editable vectors, with a Regenerate from Data command. Fully live
  charts are in HIGH-EFFORT.md.

## 13. "Copy as Layers" from the Chromium extension
- The Paper Snapshot-style route in the handoff queue: the extension copies
  a page selection as Omastrator layers, and Paste imports them.
- Lift already covers most of this inside design mode.
