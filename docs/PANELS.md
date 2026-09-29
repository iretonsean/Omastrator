# Properties panel layout

The author picked this on 2026-09-28, from the mock-ups at
https://claude.ai/artifact/Jm2gU67JnMHBcTmDJKVAZk. They chose **A + C**:
Figma's dense layout, plus one-line summaries on the sections that are
folded. The aim is less vertical scrolling without hiding features behind a
••• button. For a frame with auto layout, the panel shrank from about 1,020 px
to about 540 px.

## A: dense, as Figma is

- **Every control is 24 px tall** (`NumberField::fieldHeight`): fields, menus,
  buttons and icon buttons.
- **A number field is one box.** Its label (or the rotation icon) and its unit
  sit inside the box, around a left-aligned value, as Figma draws them.
  Dragging the label still scrubs. `NumberField` paints the box itself, with
  the style's line-edit panel, so the theme's hover and focus fills apply.
- **Fields go in pairs:**
  - Transform: X|Y; W|H with the link; rotation with the reference point,
    drawn compact.
  - Stroke: Weight|Align, Cap|Corner, Profile|Dashes, then the arrowheads.
  - Blend mode, the opacity slider and its percentage share one row.
- **Layout (auto layout):** the flow menu and − (remove) sit in the section
  heading. The alignment grid sits beside the gap and the padding. Absolute
  position and Clip content share a row.
  - Padding is a pair, ↔ (left and right) and ↕ (top and bottom). The box
    icon beside it swaps the pair for four fields, L|T over R|B, in the same
    24 px rows. It opens by itself when the sides differ, as the corner
    radius does, and each field or scrub is one "Padding" undo step.

## C: folded sections keep a summary

- A folded section's heading shows what's inside, right-aligned and elided.
  Clicking the summary opens the section. `PanelSection::summary` supplies the
  text, and the panel refreshes it on every change.
  - Transform: "20, 20 · 40 × 30"
  - Layout: "Vertical · gap 4"
  - Shape: "Radius 0"
  - Appearance: "Fill FFFFFF · Stroke None"
  - Stroke: "1 pt · Center · Butt"
  - Align: the target, such as "To selection"
  - Pathfinder: its four operations
- **Align and Pathfinder start folded** (`PanelSection(..., folded = true)`).
  They're the long tail. Anything the user folds or opens is remembered, as
  before (`properties/collapsed/<key>`).
- Sections still show and hide by what's selected.

Not changed: which sections exist, their order, every control and its object
name (tests and the agent tools find them by name), and the Document section's
contents.
