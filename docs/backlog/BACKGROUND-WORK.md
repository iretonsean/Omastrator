# Heavy work off the UI thread (backlog, 2026-09-28)

**Status: approved for the backlog by the author on 2026-09-28. Not
scheduled.**

## Why
Everything that touches the document runs on the one UI thread:
`EditorSession`, `VectorDocument`, undo, canvas drawing, imports, exports
and Image Trace. A large PDF or Figma import, Export for Screens, Image
Trace or a big Pathfinder freezes the window until it finishes. EPS import
can block for up to 60 s while Ghostscript runs (the PDF review, S9). The
parked Inspect sluggishness is probably the same problem.

Some work is already threaded: `LiftJob`, dictation, the island, and the
HSL blends through `Rendering/PoolMap.h`.

## The rule
**Don't make the document model multithreaded.** `VectorDocument` and
`EditorSession` aren't thread-safe, and undo has to stay ordered. The
document keeps one owner, the UI thread. A heavy job:
1. copies what it needs;
2. runs on a worker (a `QThreadPool` task or a `LiftJob`-style object),
   with progress and **Cancel**;
3. hands back a result, which the UI thread applies as **one named undo
   step**, or opens as a new tab.

## In order of payoff
1. **Imports:** PDF, AI, EPS (Ghostscript as a cancellable process),
   Figma, Sketch and Penpot. These are the easiest to isolate, because they
   make a new document. The UI shows an "Opening…" tab with a progress
   line and Cancel.
2. **Exports and Export for Screens.** Each file renders on a worker, the
   sheet shows progress, and Cancel stops what's left.
3. **Image Trace and big Pathfinder operations,** as a proposal the user
   keeps or discards, as they do today.
4. **Tiled parallel rendering,** for exports first and then the canvas.
   This is the most work. It needs a memory budget (see the canvas-limit
   guards: 16 MP masks, 200 MP raster exports), because the machine has
   15 GB and /tmp is RAM.

## Risks to design for
- **Qt text:** font and text shaping is only partly safe across threads.
  Keep text layout on the UI thread, or give each worker its own font
  objects.
- **Memory:** cap how many workers render at once.
- **Tests:** every worker needs a deterministic "wait for it" hook, so
  tests don't get flaky.

## Done when
Opening a 100-page PDF or running Export for Screens on 50 artboards
never freezes the window, and Cancel works mid-job.
