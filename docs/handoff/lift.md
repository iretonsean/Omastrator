# Handoff: Lift into vectors (ANYWHERE phase 2)

Branch `worktree-agent-a134ba2244fa34e0a`, 2026-09-27. The decisions are in
[docs/ANYWHERE.md](../ANYWHERE.md) under "Decisions: Lift into vectors".

## Done

- `src/Anywhere/LiftScript.h`: the page script. It walks the DOM and returns
  flattened nodes in paint order: boxes, gradients, borders, radii, sharp
  shadows, text lines with runs, images, inline SVG, transforms, opacity,
  clips and selectors.
- `src/Anywhere/Lift.cpp`: `Lift::fromDom`, which turns the page's answer into
  a group tree in page coordinates.
- `src/Anywhere/Lift+Screen.cpp`: the AT-SPI tree helper,
  `Lift::fromAccessible` (the tree over a screenshot) and `Lift::fromTrace`
  (ImageTrace, eight colours).
- `src/Anywhere/Lift+Job.cpp`: `LiftJob`, which runs in the background with a
  stage, progress and cancel. Pictures come through `Page.getResourceContent`,
  with a screenshot fallback. The tree, screenshot and trace are read on a
  worker thread.
- `VectorObject::liftedFrom`, saved in `.omai` as `liftedFrom`: the selector,
  or the accessible path.
- `OverlayStore::place`, `Desk::Frame::step`, and
  `DesignController::startLift`/`landLift`: overlay, Desk or document, each as
  one step named "Lift <thing>".
- The bar: Lift is on for web and window, Ask Agent to Clean Up appears for a
  traced lift, and the bar shows progress with Cancel (`OverlayLogic.liftText`).
- CLI: `omastrator design lift [--target N] [--region X,Y,W,H] [--to …]` and
  `lift cancel`.
- Tests: `tests/Anywhere/LiftTests.cpp` (a headless Chromium card, the
  viewport and a transform, cancel, a fake tree, the trace fallback, and a
  check that the helper is valid Python), a new
  `DesignModeUiTests::liftLandsWhereTheUserChoosesAsOneUndoStep` (overlay, undo,
  Desk, remembered destination, document, clean-up chip, busy and cancel), and
  updates to the bar tests and ShellPluginTests.

## Left / next steps

- Not tried on a lit screen: the overlay's progress row, and real AT-SPI on a
  large GTK app. Timing is unmeasured.
- Blurred and inset shadows, text shadows, pseudo-elements, clip-path, masks
  and tiled backgrounds aren't lifted.
- There is no region drag in the overlay yet. The CLI takes `--region`, and the
  QML could add a "Lift Region" tool that drags a rectangle and calls it.
- Apply to source (phase 4) can map objects back through `liftedFrom`.
