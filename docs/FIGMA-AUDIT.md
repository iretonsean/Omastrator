# Figma's design core: what Omastrator has (audit, 2026-09-27)

Omastrator is a Figma/Illustrator hybrid (docs/VISION.md), so every Figma design
feature is in scope. This audit is the build order's starting point. It was
checked against the code on `feat/graphite-look` (0999e92).

| Feature | State | Where, or what's missing |
|---|---|---|
| Frames | **built** (feat/frames) | `ObjectKind::frame`: a box (`shape`), fills and strokes, radii, Clip Content, the Frame tool (F), Frame Selection (Ctrl+Alt+G), canvas labels. Frame presets (Properties ▸ Frame while F is active: phone, tablet, desktop, social and paper sizes, plus your own). Still to come: drag into or out of a frame, Figma's resize |
| Auto layout | **built** (feat/frames) | docs/AUTO-LAYOUT.md: direction, gap/Auto, padding, alignment, wrap, Fixed/Hug/Fill, Absolute, Shift+A, the Layout section, per-side padding fields. Still to come: drag to reorder, baseline, min/max, canvas handles |
| Constraints and resizing | **built** (feat/frames) | Left/Right/Left & Right/Center/Scale per axis; box resizes on frames follow them (`VectorDocument::resizeFrame`), and never scale type: point type keeps its size, area type (also inside a group) gets a new box; imported translated boxes, a child selected with its frame, a handle pulled past the other side and Transform Again all resize this way. Only the Scale tool scales everything. What's drawn over a frame goes in it, and a move drops the selection into the frame under the pointer or out of it. An artboard resizes the same way on the Select tool, its art following its constraints and riding along when it moves (`VectorDocument::constrainToBox`) |
| Boolean groups (live) | partial | Pathfinder operations are destructive (`PathOperations`); no live boolean node |
| Components | partial | main components, variants, instances, overrides, detach, reset (`Components.h`); no component properties (boolean, text, instance swap) |
| Styles and variables | partial | text styles; design tokens with modes (`DesignTokens.h`); swatches. No colour or effect styles as their own objects |
| Multiple fills and strokes | mostly there | `extraFills`/`extraStrokes`, stroke alignment, dashes; no per-side stroke weights |
| Effects | missing | drop shadow, inner shadow, layer blur, background blur |
| Blend, opacity, masks | there | `LayerBlendMode`, opacity, clip and opacity masks |
| Corners | partial | per-corner radius and style (`LiveRectangle`); no corner smoothing |
| Layout grids | missing | columns, rows and grid on frames |
| Per-layer export | there | Export for Screens (`ScreenExport`), `exportAssets` |
| Prototyping | missing | links between frames |
| Text boxes | there | auto width, auto height, fixed (`TextContent::area`), styles, OpenType |
| Canvas UX | partial | artboard labels, Alt distances and rulers are there. Keys are compared in docs/SHORTCUTS.md (Ctrl+Alt+G, Shift+A and K are in). Missing: tidy up, frame labels (done with frames) |
| Pages | **built** (feat/pages) | docs/PAGES.md: a page is its own canvas of layers, artboards and guides. The Pages list in Layers, Object ▸ Pages, Move to Page, Alt+PageUp/PageDown, Ctrl+K, the agent's `page` tool. PDF, Figma, Sketch and Penpot imports and PDF export make and use pages. Still to come: workspaces (phase 2) |
| Image fills | missing | `PaintKind` has no image; images are only their own layers |
| Vector networks | missing | classic contour paths |

**Build order** (the author's order, with the audit's gaps after it):

1. frames
2. auto layout
3. constraints and resizing
4. effects
5. live boolean groups
6. per-side strokes
7. layout grids
8. image fills
9. pages
10. component properties
11. colour and effect styles
12. corner smoothing
13. prototyping
14. vector networks

The editable lift (HANDOFF queue item 4) imports flexbox as auto-layout frames,
so it comes after items 1 and 2.
