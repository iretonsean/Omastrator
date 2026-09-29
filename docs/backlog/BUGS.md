# Bugs found while recording the Browser View video (2026-09-29)

These were found while recording the Browser View promo from the
`feat/live-in-frame` branch (97d86b0). The app ran offscreen on Ubuntu 24.04
with Qt 6.4.2 and Playwright's Chromium, and a script drove it. Each item
says what was seen and how sure it is. Tick an item when it's fixed, as
QUICK-WINS.md does: `[x] … (a1b2c3d, date)`.

## 1. [ ] The build fails on Qt 6.4, the minimum AGENTS.md names

**Confirmed.** It's on `main` (1c3c6f7) and on `feat/live-in-frame`. CI
doesn't catch it, because CI builds in an Arch container with a newer Qt.
Each one is a one-line fix, and all four let the app build and run:

| File | Error on Qt 6.4.2 | Fix |
|---|---|---|
| `src/IO/PdfObject.h:50` | `m_bytes == literal`: ambiguous `operator==` for `QByteArray` and `QLatin1StringView` | Compare with `QByteArrayView(literal.data(), literal.size())` |
| `src/Live/BrowserLink.h` | `QPointer<QLocalSocket>` with only a forward declaration: invalid `static_cast` from `QObject*` | `#include <QLocalSocket>` in place of `class QLocalSocket;` |
| `src/UI/DesignController+Look.cpp` | `QJsonDocument` is incomplete | `#include <QJsonDocument>` |
| `src/UI/PageWorkspaces.cpp` | `QWindow` is incomplete (`sizeof` in `qmetatype.h`) | `#include <QWindow>` |

The cause is includes that newer Qt headers pull in by themselves. The
lasting fix is one of these:

- a CI job that builds against Qt 6.4 (for example Ubuntu 24.04's
  `qt6-base-dev`);
- raising the minimum in AGENTS.md and in `find_package(Qt6 6.4 …)`.

## 2. [x] A failed Save says "Deploy failed" in the frame's bar (2026-09-29)

**Confirmed in code and on screen.** Choose Save (not Deploy) from a
Browser View's bar menu. When the save fails, the bar's pill says
"Deploy failed".

- `src/UI/BrowserViews+Deploy.cpp:100` sets
  `bar.deploy = "Deploy failed"` whenever `state.stage == "failed"`. It
  doesn't check whether the run was a deploy.
- `AgentBridge::DeployState::deployed` exists, and `liveSave` sets
  `request.deploy = false`, so the bar can tell the two apart. "Save failed"
  would be correct.

**To reproduce:**
1. Start Live on your own site with no agent available.
2. Make one text edit and one style edit that the deterministic path can't
   write (for example `color` on an element with no rule of its own).
3. Choose Save.
4. The pill says "Deploy failed". Its tooltip says "1 edits weren't
   certain enough to write directly, and the agent couldn't take them: …".

## 3. [x] A failed Save leaves the certain edits written but not committed (2026-09-29)

**Seen once. Check whether it's intended.** In the case in item 2, the text
edit was written to `index.html`, but the save failed before the commit. So:

- `git status` showed ` M index.html`, and nothing was committed;
- the bar only said the save failed. It didn't say that part of the change
  is now on disk.

Decide one of these:
- roll the written part back when the save fails;
- commit the part that was written;
- say plainly that it's written but not committed.

Also check that a later Save, once an agent is available, doesn't write the
text edit twice.

## 4. [x] Two messages that don't fit (2026-09-29)

**Confirmed, minor.**
- The failure message in item 2 says "1 edits"
  (`src/UI/AgentBridge+Live.cpp:81`). It should use the singular for one
  edit.
- The Live panel (Review Changes) says "GitHub: install the gh CLI to keep
  history there (sudo pacman -S github-cli)." It shows this whenever `gh` is
  missing, even when the project already pushes to a remote that isn't
  GitHub (`src/UI/LivePanel.cpp:234`). Consider showing it only when the
  project has no remote, or when its remote is on GitHub.

## 5. [ ] A Browser View drawn at a fractional zoom gets a fractional design width

**Confirmed.** At the default zoom, a Browser View drawn with the tool came
out 1279.67 × 801.11 (the Transform section shows it). "The design width is
the frame's width" (BROWSER-VIEW.md, section 1), so the page lays out at a
fractional CSS width. Meanwhile the breakpoint row's dotted button rounds it
to "1280" (`lround` in `EditorCanvas+Breakpoints.cpp:48`), so the button
and the real width disagree.

Suggested fix: round a Browser View's width and height to whole CSS px when
the tool (and a resize) commits them.

## 6. [x] Edit Page fills every selection box after the first with solid blue (2026-09-29)

**Confirmed, with the fix tested.** In Edit Page, Shift-click a second
element. Its selection box is drawn filled with the accent colour, and the
element disappears under it. Only the first box is drawn as an outline.

- The cause is in `EditorCanvas::State::drawEditPage`
  (`src/Canvas/EditorCanvas+EditPage.cpp`, the loop over
  `boxes.selection`). Each pass sets `painter.setBrush(color)` to draw the
  label's pill, and the next pass's `painter.drawRect(rect)` still uses that
  brush.
- **The fix:** call `painter.setBrush(Qt::NoBrush);` before
  `painter.drawRect(rect);` in the loop. With that line, both boxes draw as
  outlines, which was checked in the recording.
- A test could grab the canvas with two selected elements and check that a
  pixel inside the second box isn't the accent colour.

## Not bugs (checked)

- **Zoom counts device pixels.** At a device pixel ratio of 2, "100%" is
  0.5 logical px per pt. That's deliberate: Actual Size sets the zoom to
  `backingScale` (`EditorSession.cpp:647`).
- **The stale picture from HANDOFF's phase 4 list didn't reproduce.** Edits
  and Save refreshed the frame's picture every time in this setup. It may
  depend on timing or on a dev server. Keep it on the list until it's
  checked on the desktop.
- **Style edits without a rule of their own go to the agent.** That's how
  `WriteBack::plan` is designed. Only text, custom properties and class swaps
  are written directly.
