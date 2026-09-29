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
  the style's line-edit panel, so the theme's hover and focus fills apply. A
  scrub closes its undo step however it ends: release, or the field being
  hidden (the selection changed), disabled or losing the grab. Undo and redo
  wait while an edit is open, so a scrub never records over an undone step.
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

## Frame presets

While the Frame tool is active, a **Frame** section leads the panel (whatever
is selected). A group menu (Phone, Tablet, Desktop, Social, Paper, and Saved
when there are any) picks which sizes the list below shows, each row a name and
its size in 24 px rows. The last group looked at is remembered
(`properties/framePresetGroup`). Decisions:

- **A click drops a frame, and the tool goes back to Select.** Figma does the
  same, and it brings up the new frame's Layout and Transform. The frame is
  centred in the view (the active artboard's middle when there is no view),
  on whole points, named for the preset ("iPhone 16"; "iPhone 16 2" when the
  name is taken). It is one "Frame" undo step, and nests in a frame it sits
  inside, as a drawn one does.
- **Your own sizes:** ⋯ ▸ Save Selected Frame as Preset… (one frame selected)
  saves its rounded size under a name; the same name replaces the saved one.
  Right-click a saved row for Rename and Delete, a built-in for Hide; Show
  Hidden Presets is in the ⋯. Names of built-ins are refused. No confirm on
  delete, as with document presets.
- **Storage:** the `frames` section of `presets.json`, shared with the welcome
  sheet's document presets (`PresetStore`). The file is read when the section
  first shows and again on each later show.
- The built-in list is in code (`FramePresets::builtIn`), in points.

## Settings that travel

Edit ▸ Export Settings… and Import Settings…, beside Preferences…, move a
setup between computers as one JSON file (`SettingsBundle`; the dialog is
`SettingsConfirmDialog`). Both offer the cloud browser first when rclone has a
remote ("Keep on cloud storage"), and This Computer for the usual file dialog.
Decisions:

- **What travels** is an allowlist in `SettingsBundle.cpp`: the preferences
  (nudge increment, history states, layer naming, JPEG quality, agent terminal
  and timeout, roast heat, Export for Screens scales and formats, the device
  format), the remapped keys, the workspace (panels shown and their layout,
  tool rail and its slots, reference point, task bar, Properties toggles and
  folded sections), the swatch library, and the two `presets.json` sections
  (document and frame presets). A key that isn't on the list is never written
  and never read, so an old or hostile file can't set anything else.
- **Never in the file:** the Figma token (`figma.json`), rclone's config and
  every cloud sign-in, the `cloud/` remembered remotes and folders, recent
  files, recent commands and colours, folders on this computer (design system
  project, Export for Screens), the device Send to a device last used,
  `anywhere.json`, `projects.json`, `setup.json` and `vocabulary.txt`. What
  isn't a preference of the designer's, or would name this machine, stays home.
- **Import replaces; it doesn't merge.** Figma and Illustrator both restore a
  settings file wholesale, and "the same keys, workspace and presets" is only
  true if what the file lacks goes back to its default. Things you have that
  the file doesn't are reset, and the confirm lists that too.
- **One confirm, with the backup kept.** The sheet lists each setting that
  differs (Now, After import, grouped as Preferences, Shortcuts, Workspace,
  Swatches, Presets); Cancel is the default, and Replace Settings is a click,
  not Enter. Settings that already match aren't listed; if none differ there is
  only a status-line notice. Before anything changes, the current settings are
  written as `settings-<time>.json` (a file Import Settings reads, so a bad
  import is undone by importing it) and `presets.json` is copied to
  `presets-<time>.json`, both in `backups/` beside `presets.json`. If the
  backup can't be written nothing changes. Old backups aren't pruned.
- **Presets keep their safety rule.** A `presets.json` that can't be read is
  never replaced: Export leaves the presets out and says so, Import applies
  everything else and says the presets stayed.
- **Remapped keys must hold together** (`ShortcutSettings::problem`), or the
  file's keys are dropped with a note and yours stay.
- **Panel layout and swatches** are read when Omastrator starts, so they appear
  after a restart; keys and the history depth apply at once.
- The Preferences dialog itself has no buttons for this; the two items sit in
  the Edit menu (and Ctrl+K) next to Preferences….
