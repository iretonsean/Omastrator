# Frames only: artboards become top-level frames (backlog, 2026-09-29)

**Status: decided by the author on 2026-09-29 (remove artboards, keep frames). Parked in the
backlog. Not scheduled.**

> **Update (2026-09-30): artboards became optional first** (branch `feat/optional-artboards`). A page may
> now have zero artboards: Delete Artboard works on the last one, New Page makes none, and export, Export for
> Screens, PDF, Share, Fit and the agent's tools fall back to the top-level frames, else the content bounds.
> This does not remove artboards, does not change the file format beyond one additive key (`artboardsListed`),
> and does not touch importers (they still make artboards). The migration below is still the plan.

## Why
An artboard and a frame do the same job today, in two models:
- `Artboard` (`VectorDocument.h`) is a flat list beside the object tree: a name, a rect, a
  paper colour, a page and an `exported` switch. It owns nothing; the art "on" it is worked out
  from geometry (`artCenteredIn`, `objectsOn`), hence the "Move art with artboard" checkbox.
- A frame is a `VectorObject` of kind `frame` in the tree: it owns its children and has fills,
  strokes, corners, clipping, auto layout, constraints and Browser View.

HANDOFF already says "an artboard behaves like a top-level frame": both resize through
`constrainToBox`. Two models mean two code paths that drift, a choice users must make, and an
artboard that can't hold what a frame can. Figma (2019), Sketch (2023) and Penpot ("boards") have
one concept, and those are the files Omastrator imports.

## The design
- **One Frame tool (F).** Drawn on the empty canvas, it makes a top-level frame: a page that
  exports. Drawn inside a frame, it nests. The Artboard tool, Shift+O and New Artboard go.
- **What artboards did that frames keep:**
  - Export on/off: the artboard's `exported` becomes a frame setting (top-level frames export
    by default).
  - Paper: the frame's fill.
  - Art that runs past the edge or across boards (Illustrator's spreads, patterns, bleed):
    **Clip content** off, so a top-level frame doesn't crop what's drawn past it. Art that
    crosses two frames stays on the canvas, outside both.
- **Old files and imports:** each artboard becomes a top-level frame with clipping off. Art
  wholly inside moves into it; art across several stays on the canvas. PDF and `.ai` pages
  become top-level frames with clipping off. Figma pages keep their frames as they are (today
  the importer wraps each page in an artboard).

## Scope
Artboards touch about 55 source files: the document and codec (a file-format migration),
EditorSession+Artboards, the canvas's Artboard tool and labels, Export for Screens, Share,
the status bar, the PDF, AI, Figma, Sketch and Penpot importers, PDF export, the agent's
tools, Pages, and their tests (ArtboardResizeTests, ArtboardSelectTests, ArtboardToolTests
and the export and import tests).

## Order when it's picked up
1. Frame settings for export on/off (clipping already exists).
2. The codec reads artboards and writes top-level frames (old files open, nothing is lost).
3. Export, Share and the status bar use top-level frames.
4. Importers, then the agent's tools.
5. Remove `Artboard`, the tool and its menu items; update SHORTCUTS.md and the tests.
