# Design sessions: one switch, every surface

Brainstormed with the author on 2026-09-27, after a day of design-mode traps
(see "Why" below). **Status: agreed direction. Nothing is built yet.** Read
`docs/VISION.md` and `docs/ANYWHERE.md` first.

> **Update, 2026-09-29: the desktop island is removed.** This note was written
> for the island as the session's one switch and one row of tools. That switch
> moves into the app window ("Design over…" replaces design mode), so wherever
> this file says "the island", read "the session's control in the app": the
> on/off switch, the tool row and the "Designing · 4 changes on 3 surfaces"
> score. The session itself (nothing taken over on entry, Save & close, Discard
> & close, Review, a journal after a crash, one undo stack per surface, and a way
> out that never asks) is unchanged. Until "Design over…" ships, design mode
> works as before with the island's role gone: Super+Alt+O or `omastrator
> island mode design` turns it on, Esc leaves, and tools are `omastrator design
> tool …`. See OS-SUITE.md, component 1.

## The idea

The island is one on/off switch for a **design session**. While a session is
on, every Omastrator tool works on any surface: a window, an app, a web page
in any Chromium, or the desktop itself. When the session ends, the user
chooses what happens to everything they did.

- **Turning it on takes nothing over.** The session starts on the Point tool.
  Clicks still reach the apps, nothing is inspected, and no bar pops up. The
  island simply offers every tool: inspect, measure, draw, capture, lift, AI,
  live edit and desktop look, on whatever is under the pointer.
- **The modes fold into the session.** Draw, Capture, AI, Live and Design stop
  being separate modes with separate Hyprland submaps. They become tool
  groups in the one island row, with one way in and one way out. Most of
  2026-09-27's traps came from each mode having its own entry and exit.
- **Live just works.** While a session is on, the page in any Chromium window
  is editable. There's no URL to type into Omastrator and nothing to start
  first. See the Live notes in the handoff.
- **The island keeps score:** "Designing · 4 changes on 3 surfaces". Unsaved
  work, and the way out, are always visible.

## Ending a session

- **Save & close** sends each change where it belongs:
  - Drawings and annotations are kept on their surface or sent to the Desk.
  - Edits to the user's own site are written into its code, with the usual
    confirmation.
  - Desktop Look changes are saved to the config, with backups and Revert.
- **Discard & close** reverts everything: drawings go, page edits roll back,
  desktop previews undo, and lifts and agent runs stop.
- **Review** is one card listing the changes by surface, each with a
  checkbox, so the user can keep some and drop others.
- **Esc and Super+Alt+Escape.** Esc ends the session and asks. The hard reset
  (`omastrator reset`, Super+Alt+Escape) stays the one path that never asks,
  and must always work.

## Decisions (the author, 2026-09-27)

1. **A crash mid-session.** The session keeps a journal of its changes. On
   the next start the island offers to restore or discard the session, and
   the user's default agent helps with the recovery (for example, reapplying
   page edits the journal couldn't restore by itself).
2. **Saving work on a site the user doesn't own**, where there's no code to
   write to. The user chooses one of:
   - a raster mock-up with the edits;
   - a live mock-up with editable layers, plus their annotations and edits;
   - the mock-up with editable layers, without the edits.
3. **Pausing.** Being on already counts as paused: Point is quiet and
   click-through, so there's no separate pause. An explicit pause (changes
   held pending while the computer is used normally) is the fallback if this
   first version doesn't work well.
4. **Undo.** There is one undo stack per surface. A **timeline panel** holds
   the session's long history, so the user can go back to a specific point
   after many edits.

## Open questions

- How the timeline panel shows several surfaces' histories side by side:
  one lane per surface, or one merged list with surface labels.
- What "restore" means for page edits whose page has since changed.
- Whether Live's page edits and overlay art share one surface history or
  keep two.

## Why

On 2026-09-27 the author was trapped several times:
- The Pen covered the island.
- Esc wasn't bound when design mode was entered from the island.
- A lift and an AI palette card stayed on screen after design mode ended.
- The floating bar covered the top bar.
- Leftover submaps kept mode letters bound (Live's `d` could deploy).

The fixes are on `fix/design-mode-escape`: `reset` and Super+Alt+Escape, the
island kept reachable, one submap rule for every mode, and cleanup whenever
a preview's owner goes. Sessions are the structural fix: one mode, one exit,
and a clear record of what changed. The rule stays: **Omastrator never takes
over the computer without an obvious way out.**
