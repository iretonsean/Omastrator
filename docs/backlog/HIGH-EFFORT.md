# High effort (2026-09-28)

Multi-phase work: a design doc first, then an agent (or several) per phase.
Two already have approved or drafted designs.

## 1. Browser View and canvas workspaces (approved)
- **Design:** docs/BROWSER-FRAMES.md, approved by the author on 2026-09-28.
- **What it covers:**
  - pages as Omarchy workspaces;
  - the Browser View frame with an editable address bar and the Browse
    tool;
  - breakpoints as a preview, not an edit;
  - Live and Deploy inside the frame, and "Build it" through the agent;
  - a headless Omastrator Chromium profile with a one-time sign-in.
- **After it ships:** Live mode is removed from the island.
- **It answers:** Figma's "real breakpoints" request (4/5) and the handoff
  gap.

## 2. Effects: shadows and blurs (waiting on the author's review)
- **Design:** docs/EFFECTS.md. Drop shadow, inner shadow, layer blur and
  background blur, in six phases.
- **Unlocks:** Figma, Sketch and Penpot imports keeping their shadows,
  Lift's box-shadows, shadow tokens, live vector halftones and effect
  styles.

## 3. Real-time co-editing
- **Demand:** Illustrator (3/5, years of threads). It's Figma's biggest
  lead over Illustrator.
- **Needs:** a sync model (probably CRDTs over the document), presence and
  cursors, a transport that fits a local-first, Linux-first app (for
  example peer to peer over a relay, or a self-hostable server), and
  conflict rules for components and history.
- Start with a design doc and a spike.

## 4. Design sessions
- **Design:** docs/SESSIONS.md, with the direction agreed and three open
  questions waiting on the author (timeline lanes, what restore means,
  shared or separate histories).

## 5. Native tables
- **Demand:** Figma (3/5, "not on roadmap"). Neither tool has them.
- **Do:** a table object with rows, columns, merged cells, cell text
  styles, resizing with auto layout, and import from CSV or pasted
  spreadsheets.

## 6. Live, data-bound charts
- It goes beyond MEDIUM-EFFORT.md's chart generator: charts that stay
  linked to a data table or file, restyle through tokens, and update when
  the data changes.

## 7. Prototyping
- **Demand:** it's missing (docs/FIGMA-AUDIT.md). Figma's reusable
  interaction and animation styles (3/5) are still "coming soon".
- **Do:** links between frames, triggers, transitions and a presenter view.
  Reusable interaction styles from the start, which is the part Figma users
  are still waiting for.

## 8. Component properties
- Boolean, text and instance-swap properties on components, beyond today's
  variants and overrides (docs/FIGMA-AUDIT.md, partial).

## 9. Performance at scale
- **Demand:** Illustrator's multithreading (782 votes, still in beta) and
  Figma's large-file slowdowns.
- **Do:** a render cache and tiles (the canvas redraws the whole document on
  every repaint today: `EditorCanvas.cpp:224`), parallel rasterization, and
  a large-file benchmark in `scripts/`.

## 10. Vector networks
- Figma's non-contour paths, where any point can join several segments.
  It's a deep change to `VectorPath`, the pen tool and every path operation.
