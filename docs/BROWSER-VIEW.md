# The Browser View frame (design, 2026-09-29)

Phase 3 of BROWSER-FRAMES.md: **the Browser View tool, the frame's controls,
streaming, the Browse tool, breakpoint buttons and resize-as-preview.** Phases
1 and 2 (PAGES.md, WORKSPACES.md) are merged. Live inside the frame and the
island (phase 4), and Duplicate at Breakpoints, pinning and Clean Session
(phase 5), are out of scope. The seams for them are named where they fall.
Nothing here is built.

**Since 2026-09-29 the Browser View tool is gone:** the Frame tool's Browser
View switch turns a frame's page on and off, and runs or freezes the project's
dev server behind it. See section 10 at the end.

## 1. The model

A Browser View is a **frame** (`ObjectKind::frame`) with a URL. It is not a new
kind: an unknown `kind` makes an older build refuse the whole file.

- `std::optional<BrowserView> browser` on `VectorObject`, set only on frames:
  - `QUrl url`: the page the frame shows. Empty means "no page yet".
  - `QPointF scroll`: the page's scroll offset in CSS px, so art drawn over
    the page lines up again after a reopen.
  - `QImage picture`: the last picture, at 1× the frame's size.
- **The design width is the frame's width**, and the viewport height is its
  height. 1 pt is 1 CSS px. There's no separate field that could drift.
  Both are whole px: the tool, the handles, Transform's W and H, a breakpoint
  and Set as Design Width all round to whole px (`BrowserView::wholeSize`, in
  `VectorDocument::resizeFrame` and where the tool draws), whatever the zoom.
  The position may stay fractional.
- **Breakpoints aren't stored.** They're read from the project, or are the
  defaults (section 7). Phase 5's Duplicate at Breakpoints reads the same list.
- `LayoutItem::previewRule` on every object: `constraints` (the default) or
  `fixed`. Phase 5 adds `pinned`, which uses `liftedFrom`.

**File format.** These keys are additive and need no version bump:

- On the frame: `"browserView": {"url", "scroll": [x, y]}`.
- On a child: `"layout": {…, "preview": "fixed"}`, written only when it isn't
  the default.
- **The picture is written as a real child**, so an older build draws it. The
  codec writes it as the frame's bottom child: an `image` object named
  "Last picture of <host>" with these keys:
  - `"locked": true` and `"layout": {"absolute": true}`, so auto layout
    doesn't flow it;
  - a transform onto the frame's box;
  - a PNG, at most 2048 px on its long side;
  - `"browserPicture": true`.

  On decode, a child with `browserPicture` is taken out of the tree and put
  into `browser->picture`. So it never shows in Layers, hit tests or history.
  An older build shows a plain frame with a locked picture in it. If the
  older build saves, the file keeps that picture as an ordinary image and the
  URL is lost. That's acceptable.
- **The clipboard leaves the picture out**, through an encode option, so
  copying a Browser View doesn't copy a megabyte. The pasted frame streams its
  URL again.

**Document state or view state:**

| Document (in the file) | View (the session, never history) |
|---|---|
| The URL, the frame's box (so the design width), each child's `previewRule` | The live pixels, the tab's back and forward stack, whether it's loading, the preview width, paused or streaming |

Only some of the document state is undoable:

- **Undo steps:** typing a URL into the address bar and pressing Enter
  ("Change URL"), setting the design width, and `previewRule`.
- **No undo step** (like `setDocumentLocked`, the file is marked modified):
  following a link while browsing, back and forward, a redirect, and the
  scroll offset. These go through `EditorSession::setBrowserLocation(id, url,
  scroll)`.
- **`picture`** is refreshed silently: when the frame pauses, before save and
  before any export. It never marks the file modified.

**When undo changes the URL**, the tab navigates to match. The rule is
"a frame's tab shows its document URL". The controller reconciles it on
`documentChanged()`.

## 2. The browser behind it

- **`BrowserPool`** (oma_live) owns one headless Chromium on the Omastrator
  profile, `Browser::defaultProfile()`. It's started lazily when the first
  frame needs a tab.
  - It launches through `Browser::chromiumArguments`, with a new
    `Options::cache` setting: `minimal` (the tests' 1-byte caches, today's
    headless default) or `capped` (the 64 MB limits). The pool passes
    `headless + capped`, so the cap in BROWSER-FRAMES.md holds.
  - It writes `<runtime dir>/browser-view.json` with the pid and profile, so
    reset can find it.
  - It stops 60 s after its last tab closes, and when the app quits.
- **One tab per frame on the current page.** Each is a `Target.createTarget`
  plus a flattened session.
  - Tabs live in the default browser context. The pool's `open(frameId,
    context = {})` takes a context id so phase 5's Clean Session can pass a
    `Target.createBrowserContext` one.
  - `Browser.setDownloadBehavior` is set to deny.
  - Downloads-deny and `Browser.setPermission` (denied) are applied to the
    default context only. A `Target.createBrowserContext` context needs both
    again, with its `browserContextId`, before its first tab loads anything.
- **Sign in to Omastrator's browser.** The first Browser View shows a strip
  inside the frame: "Sign in to Omastrator's browser to see sites you're
  logged in to." with the buttons **Sign In…** and **Not Now**.
  - Sign In… stops the headless browser (a profile opens in one process at a
    time) and launches the same profile in a normal window through
    `chromium-flags.conf`, the way `Browser` starts a windowed browser today.
  - While that window is open, frames show their last picture and "Signing
    in…". When the window closes, the pool restarts headless and the frames
    resume.
  - Either button records `browserView/signInOffered`. Later it's in the
    frame's right-click menu.
- **Live's windowed browser uses the same profile.** While it runs, frames
  pause with "Omastrator's browser is open for Live. Frames resume when it
  closes." Phase 4 removes this case by moving Live into the frame.
- **Without Chromium** (`Browser::executable()` is empty, or the launch
  fails), frames draw their last picture, or their fill, with a plain line:
  "Chromium isn't installed, so this shows the last picture." The URL can
  still be edited and everything else works.
- **Tab lifetime.** The controller reconciles on `documentChanged`, page
  switches and viewport changes:
  - **Pause:** the frame is off screen, it's narrower than 160 px on screen,
    it's on another page, or the window is hidden. The screencast stops, and
    `Page.setWebLifecycleState frozen` saves CPU. The frame shows its last
    picture.
  - **Resume:** `Page.setWebLifecycleState active`, then
    `Emulation.setFocusEmulationEnabled true`. Chromium hides the page when
    it freezes it and doesn't show it again when it becomes active, and a
    hidden page paints no more screencast frames, so without this the frame
    kept the picture it had when it came back. Focus emulation shows the
    page. The pause turns it off before the freeze, because a page that
    emulates focus stays visible and doesn't freeze.
  - **Close:** the frame is deleted, undone away, or its document closes.
    Undo or redo that brings a frame back opens a new tab at its URL. The
    back and forward stack is lost.
  - **At most 8 tabs are open.** Past that, the paused tab shown least
    recently closes. Its frame keeps its URL and picture, and reloads when
    shown.
  - **Escape hatches.** The app's `design reset` (so `omastrator reset` and
    Super+Alt+Escape) closes every frame's tab and stops the pool. Frames
    then say "Paused by reset. Click to resume." `runReset` also kills a
    pool left by a crash, from `browser-view.json`, only if that pid's
    cmdline names our profile. A reset never restarts anything by itself.

## 3. Streaming

- **Off the UI thread.** The pool, its `Browser`, `WebSocketClient` and
  `CdpConnection` live on a dedicated `QThread`. They already use nested
  event loops, which is fine there.
  - The UI calls the pool through queued invocations and receives decoded
    `QImage`s through queued signals.
  - Browser View code never uses `callAndWait` on the UI thread.
  - Phase 4's LiveSession-in-a-frame reaches a tab through
    `BrowserPool::session(frameId)`, running on the pool's thread.
- **Size.** `Emulation.setDeviceMetricsOverride {width, height,
  deviceScaleFactor, mobile: false}` with the frame's size in CSS px.
  - `mobile: false` is what the rendering test showed pages without a viewport
    tag need.
  - `deviceScaleFactor` is 1, or 2 once the zoom times the screen's ratio goes
    past 1.5. It changes only after 300 ms settled, since it re-rasters the
    page.
- **Pixels.** `Page.startScreencast {format: "jpeg", quality: 80, maxWidth,
  maxHeight}`.
  - `maxWidth` and `maxHeight` are the frame's size on screen in device
    pixels, rounded up to 64, and at most 2560. A zoom that has settled for
    150 ms restarts the screencast at the new size. So does a change of the
    page's CSS size, since Chromium sends no reflowed frame after a metrics
    override on its own.
  - Each `Page.screencastFrame` goes to a decode worker, with one decode in
    flight per frame. The newer frame wins, and the ack is sent when the
    decode finishes. That's the back-pressure.
  - `metadata.scrollOffsetX/Y` updates `scroll`, through
    `setBrowserLocation`, coalesced to 1 s.
- **To the canvas.** oma_canvas doesn't link oma_live, so the canvas talks to
  a small abstract `BrowserViewHost` declared in src/Canvas. The UI's
  `BrowserViews` controller implements it with `picture(id)`,
  `chrome(id)` (section 4's state) and the input calls (section 5).
  - `VectorRenderer::Options` gains `std::function<QImage(const QUuid&)>
    livePicture`. For a Browser View the renderer draws the frame's fills,
    then the live image (else `browser->picture`) fitted to the box under the
    clip, then the children.
  - A new frame calls `update(viewRect)`. Repaints are coalesced to 60 Hz by
    one timer.
  - Exports, the stand-ins, the agent's `render` and the overlays pass no
    callback, so they draw the last picture.
- **Budget.**
  - Frame rate: the frame being browsed or selected gets every frame (up to
    60 fps). Others get a picture at most every 33 ms, or 66 ms once more than
    four are streaming. Chromium always sends every frame, and the gap is kept
    on our side with the newest frame waiting (see "Decided while building").
  - Memory: one decoded image and one picture per frame, about 4 MB each at
    1280 × 800 and 26 MB at the 2560 cap. Plus about 100 to 200 MB of
    Chromium renderer per open tab.
  - **Ten frames on a page:** the ones on screen stream, the rest are paused
    or closed, and the tab cap of 8 bounds Chromium at roughly 1.5 GB in the
    worst case.

## 4. The controls

**Canvas chrome at a constant screen size**, where the frame label sits: a
28 px bar above the frame, the frame's width on screen. It isn't the task bar:
the controls work with any tool and without a selection, and Browse needs them
unselected. From left to right:

- **Back, forward, reload** (stop while loading), as 24 px icon buttons.
- **The address field:** host and path, elided, with the frame's name before
  them in the label style. A click opens an inline `QLineEdit` over it, as
  `InlineTextEditor` does for text:
  - Enter commits "Change URL" and Esc cancels.
  - Only http and https are accepted. A bare host becomes `https://`, and
    `localhost` or `127.0.0.1` becomes `http://`.
  - Anything else is refused in the status line: "Browser View opens http and
    https pages only."
- **Breakpoint buttons,** shown only while the frame is selected, hovered or
  browsed (the essentials first). Each is a width label ("390", "768"…),
  plus the design width marked with a dot.
- **A 2 px loading line** under the bar.
- **"Not your site",** a small tag at the bar's right end (section 8).

Below 240 px on screen the bar collapses to the ordinary frame label with a
globe icon. Hit-testing the bar comes first in `State::press`, before the tool
switch, as the frame labels are.

**One step away**, in the bar's right-click menu (also in Ctrl+K as "Browser
View: …"):

- Copy URL
- Open in My Chromium (`xdg-open`)
- Set as Design Width
- Reload Ignoring Cache
- Sign in to Omastrator's browser…

No new Properties section is added. Transform's W and H set the design width
and height (undoable). Layout gains the "Fixed while previewing" checkbox for
children of a Browser View (section 6).

**The tool.** *Replaced on 2026-09-29 by the Frame tool's Browser View switch
(section 10). What follows is how it was built.* `Tool::browserView` is appended
to `Tool`, and `toolInfo` grows. It sat in the rail's Frame group,
`{frame, browserView, artboard}`, with no default key (it can be remapped).

- Dragging draws the frame. A click drops a 1280 × 800 one, and the Frame
  presets section works for it as for Frame.
- After it's drawn, the tool returns to Selection and the address field opens
  with the placeholder "Type a URL". An empty frame shows its fill and "No
  page yet."
- It nests in a frame it sits inside, as Frame does.

## 5. The Browse tool

`Tool::browse`, an arrow in the rail's Selection group, with no default key.

- **While it's on:**
  - Presses, moves and releases over a Browser View go to its page as
    `Input.dispatchMouseEvent`, with button, `clickCount` and modifiers.
  - The wheel goes as `mouseWheel` (Qt's angle delta, 1/8° to 1 px as
    Chromium does).
  - Canvas points map to CSS px as `(point − frameTopLeft)`, using the preview
    box while one is showing.
  - Nothing in the design is hit-tested, selected or dragged. Over empty
    canvas, Browse pans (Figma's hand).
- **Keys** go to the page only after a click has put focus in a frame, and
  until Browse ends or the user clicks outside it:
  - `Input.dispatchKeyEvent` sends keyDown and keyUp with `key`, `code`,
    `windowsVirtualKeyCode` and modifiers.
  - Text goes through `Input.insertText`, which covers IME commits.
  - **Esc** always leaves Browse for Selection. It's a new first branch in
    `State::keyPress`. **Ctrl+K** always opens the palette. Everything else,
    Ctrl+Z included, goes to the page, because a text field on the page needs
    it.
  - The cost: a site's own Esc can't be reached. Its close button can.
- **The page never takes more than that.**
  - Leaving Browse stops dispatch.
  - Pointer lock, fullscreen and notifications are denied through
    `Browser.setPermission`.
  - `alert`, `confirm` and `prompt` are dismissed through
    `Page.handleJavaScriptDialog`, and their text shows once in the bar.
  - A popup (`target=_blank`, `window.open`) closes its target and opens the
    URL in the same frame.
  - File choosers are refused: "Uploads aren't supported in Browser View
    yet."
  - The cursor stays an arrow, since headless Chromium doesn't report one.
- **The frame's controls work with every tool,** Browse included. Other tools
  design over the frame as over any frame.

## 6. Resize is a preview

- **Dragging a Browser View's handles** is `beginInteraction("Preview
  Width")`, then `previewDocument`. Width and height reflow the site live
  (the metrics override follows every step, and the screencast every settled
  step). Release always ends with `cancelInteraction()`, so the frame returns
  to the design width and no history step is made. Moving the frame is still
  an ordinary Move step.
- **To change the design width:** Transform's W, or Set as Design Width
  while a preview is showing (from a breakpoint button's right-click, or the
  bar's menu). Either one is `resizeFrame` in one step, "Design Width".
- **The children during a preview.** `constraints` is the default: children
  follow `constrainToBox` and auto layout, as a frame's do.
  - Frames already default to left and top constraints, so a plain drawing
    holds still.
  - Content built with auto layout, and lifted flexbox, reflows as the site
    does. That's the point of checking breakpoints.
  - `fixed` keeps an object exactly at its design-width place. It's for notes
    and callouts.
  - A fixed child is left out of the `constrainToBox` call and of the flow,
    as if it were absolute.

## 7. Breakpoints

- **Your own site** (`ProjectRegistry::folderFor(url)` isn't empty): the
  widths come from the page's stylesheets, read the way token snapping reads
  them. A script like overlay.js's `scan()` runs in the frame's tab and
  collects:
  - each `CSSMediaRule`'s `min-width` and `max-width`, and the range syntax
    Tailwind v4 compiles to (`width >= 40rem`);
  - rem converted with the root font size;
  - Tailwind's `--breakpoint-*` theme variables.

  They're deduplicated, 320 to 2560 px are kept, and at most the 5 most-used
  are shown, sorted. They're cached per origin for the session and read again
  on reload. The pure parser, `Breakpoints::fromScan(json)` in oma_live, is
  tested without a browser.
- **Otherwise, or when nothing is found:** 390, 768, 1280, 1440.
- **A button is a held preview.** The frame shows that width until the same
  button, the design-width button, Esc or a new drag. It's the same
  begin/preview/cancel as a drag, held open, so nothing is recorded.
  Switching tools or pages ends it.

## 8. Sites that aren't yours

Today's rules (ANYWHERE.md): edits stay on this machine and there's no Deploy.
Phase 3 doesn't edit pages, so the rules show in two places:

- the bar's **"Not your site"** tag, whose tooltip is "Not your site: changes
  stay on this machine.";
- the breakpoints falling back to the defaults.

Ownership is the registry's test, shared with `LiveSession::isMockup`. Factor
it into one `ProjectRegistry::owns(url)`. Phase 4 brings the strip, edit sets
and Deploy's refusal into the frame.

## 9. Tests

Tests skip without Chromium (`QSKIP` when `Browser::executable()` is empty),
as the Live tests do. They serve a new fixture,
`tests/Live/fixtures/breakpoints/`, through `StaticServer`: `@media` at
`min-width: 768px`, at `width >= 64rem`, and at `--breakpoint-xl`, with a
`#w` element showing `innerWidth` and a button that changes text.

Isolation:

- `XDG_DATA_HOME`, `XDG_CONFIG_HOME` and `OMASTRATOR_RUNTIME_DIR` point at
  temporary folders, so the profile and `projects.json` are throwaway.
- The pool uses `Options::cache = minimal`.
- Nothing reaches the network or the user's own profile.

The tests:

- **`DocumentCodecTests`:**
  - `browserView` and `preview` round-trip;
  - the picture is written as a locked, absolute `image` child and taken back
    out on read;
  - JSON with `browserPicture` removed (an older build's view) loads as a
    frame with an image child;
  - the clipboard has no picture.
- **`BrowserArgumentsTests`:** headless plus capped gives the 64 MB flags, and
  headless plus minimal gives 1 byte.
- **`BreakpointsTests`** (pure): px, rem, range syntax, `max-width`, the
  Tailwind variables, deduplication, the 320 to 2560 clamp, the cap of 5, and
  empty giving the defaults.
- **`BrowserInputTests`** (pure):
  - the canvas-to-CSS-px mapping under zoom and under a preview;
  - Qt keys to `key`, `code` and VK;
  - Esc and Ctrl+K never dispatched;
  - URL normalising and refusing (`file:`, `javascript:`, `data:`,
    `chrome:`).
- **`BrowserPoolTests`** (Chromium):
  - lazy start;
  - one target per `open`;
  - `close` removes it (`Target.getTargets`);
  - the 8-tab cap evicts the least recently shown;
  - stops when idle;
  - `browser-view.json` is written and cleared;
  - runs on its own thread (asserting `QThread::currentThread()` in the
    reply).
- **`BrowserViewTests`** (UI, Chromium):
  - drawing a frame and setting the URL gives a live picture within 5 s, with
    `#w` equal to the frame's width;
  - "Change URL" is one undo step, and undo navigates back;
  - a link click records nothing but marks the file modified;
  - a handle drag to 390 makes `#w` read 390 with no history step, and release
    restores the width;
  - Set as Design Width is one step;
  - a registered fixture origin gives 768/1024/1280 buttons, and an
    unregistered one gives the defaults and the tag;
  - off-screen, below 160 px and on another page all stop the screencast;
  - delete closes the tab, and undo reopens it;
  - closing the document and `design reset` close every tab.
- **`BrowseToolTests`** (UI, Chromium):
  - a click changes the button's text, and nothing is selected;
  - typing reaches an input;
  - Ctrl+K opens the palette;
  - Esc returns to Selection;
  - a popup opens in the same frame, and an `alert` is dismissed.
- **No Chromium** (`OMASTRATOR_CHROMIUM=/nonexistent`): the frame draws its
  picture and the message, and editing, saving and exporting work.
- **`VectorRendererTests`:** an export draws `browser->picture` under the
  frame's clip. A live picture of the frame's size fills it (at 390 wide,
  every corner of the phone layout lands on the frame's). A stale one, in a
  box larger or smaller than the page it shows, is drawn 1:1 from the top
  left and clipped, at zoom 1 and 2.
- **`LiveFramePictureTests`** (UI, Chromium): after a pause and resume (the
  frame's page shown and back), a change in the page reaches what the canvas
  draws, twice over. The dev-server half is in docs/LIVE-IN-FRAME.md.
- **`WorkspaceClaimsTests`** (or a new `ResetTests`): `runReset` kills a pid
  from `browser-view.json` only when its cmdline names the profile. The test
  uses a fake long-running script.

## Build plan (each commit shippable)

1. **Model and file format:** `BrowserView`, `previewRule`, the codec (the
   picture child, the clipboard option), and the renderer drawing
   `browser->picture`. Codec and renderer tests. Nothing creates one yet.
2. **`BrowserPool`:** `Options::cache`, its own thread, tabs, idle stop,
   `browser-view.json`, and reset (app and `runReset`). Arguments, pool and
   reset tests.
3. **Streaming and the controller:**
   - `BrowserViewHost` and `BrowserViews`;
   - the metrics override, the screencast, decode and ack, the repaint
     coalescing;
   - pause and close reconciliation, the no-Chromium message and the Live
     conflict.

   `BrowserViewTests`, the streaming half. A Browser View made in a file or
   by the agent now shows live.
4. **The tool and the bar:** `Tool::browserView`, the rail and icons, the bar
   (address, back, forward, reload, loading, the tag), "Change URL",
   `setBrowserLocation`, the menu and Ctrl+K, and the sign-in strip.
5. **The Browse tool:** input, keys, Esc, permissions, dialogs, popups and
   file choosers. `BrowserInputTests` and `BrowseToolTests`.
6. **Resize-as-preview and breakpoints:** handle previews, `Breakpoints` and
   its scan, the buttons as held previews, Set as Design Width, and "Fixed
   while previewing" in Layout. `BreakpointsTests` and the rest of
   `BrowserViewTests`.
7. **Docs:**
   - BROWSER-FRAMES.md: phase 3 built.
   - ANYWHERE.md's escape hatches: reset closes the frames' tabs.
   - AGENTS.md: `BrowserPool` under src/Live, `BrowserViews` under src/UI.
   - The user guide.

## Decided by the lead (2026-09-29; the author can reverse it)

- **The sign-in window is offered, not opened.** BROWSER-FRAMES.md says the
  first Browser View *opens* the profile in a normal window. It's offered
  inside the frame instead (Sign In… / Not Now), because a window that opens
  by itself takes the keyboard and a workspace slot. The author's
  escape-hatch rule is that nothing grabs either. Opening it on the first
  frame is a one-line change if the author prefers it.

## Decided while building

- **Model (commit 1).**
  - The picture child's id is a UUIDv5 of the frame's id, so it's stable
    across saves.
  - A stored picture is scaled to at most 2048 px on its long side.
  - The renderer stretches the stored picture over the frame's box. A live
    picture is stretched only while it depicts the frame's size; a stale one
    is drawn 1:1, top-left and clipped, until the reflowed one arrives.
  - The clipboard, the agent's reads and the library leave pictures out;
    the SVG export embeds the last picture.
  - A `browserView` URL that doesn't parse reads as "no page yet".
- **BrowserPool (commit 2).**
  - `open(frameId, context)` opens an `about:blank` tab, attached with Page
    enabled, and emits `opened`. Navigating is a `call`, so the controller
    sets the metrics before the first load.
  - Past the 8-tab cap the paused tab shown least recently closes
    (`CloseReason::evicted`). If every tab is on screen the oldest goes,
    since refusing the newest would leave a frame that can't open.
  - A Chromium that exits on its own closes every tab with
    `CloseReason::lost`. The frames can then open again.
  - The state file's code (`BrowserPoolState`) lives in oma_agent, not
    oma_live, because `runReset` is in oma_agent and oma_live links it, not
    the other way round. `runReset` ends the browser after the app's reset
    (and if the app didn't answer), so a hung pool goes too.
  - The app's own reset calls `closeAll` on the pool from `BrowserViews`
    (commit 3), the first place the app owns one.
- **Streaming and the controller (commit 3).**
  - `BrowserViews` (in oma_ui) is the controller: one per `EditorSession`,
    one pool for the app. The canvas only knows the abstract
    `BrowserViewHost` (a picture and a message per frame), so oma_canvas
    doesn't link oma_live.
  - The pool has no parent: an object with a parent can't move to its own
    thread. It is deleted on `aboutToQuit`.
  - A page's own navigation is written to the frame's address with
    `setBrowserLocation`. It is no undo step, but it marks the file unsaved,
    and history snapshots holding the old address follow it, so an undo never
    steers the tab back.
  - Scrolling alone is silent: it doesn't mark the file unsaved. The scroll
    and the last picture are saved with the next edit or save.
    `flushPictures` runs on save, export, Export for Screens and the Desk's
    autosave, so a picture reaches the file without an edit.
  - "Click to resume" after a reset is a selection: selecting a paused frame
    resumes it. Nothing else brings it back.
  - An `OMASTRATOR_CHROMIUM` that isn't executable counts as no Chromium.
  - Frames get a picture at most every 33 ms (66 ms when more than 4
    stream), and every frame for the selected one. Hover doesn't count.
    (Commit 5 replaced `everyNthFrame`: see below.)
  - While Live has its own window, the pool is stopped first
    (`closeAll(true)` blocks until it is), since both use one profile. The
    frames show "Omastrator's browser is open for Live" and resume after.
  - A reset asked while the browser is still starting stops it as soon as it
    is up.
  - The "no page", "not installed" and paused messages are drawn by the
    canvas over the frame, in the palette's window colours.
  - `closeAll(true)`, used only when Live opens its window, closes tabs with
    `CloseReason::closed` and not `reset`, so a late event can't mark the
    frames "paused by reset". If the browser is still starting it stops just
    after it is up, so the profile can be busy for that moment.
- **The tool and the bar (commit 4).**
  - The address bar is drawn by the canvas above the frame, 28 px tall, and
    the address is a `QLineEdit` only while it is edited. The bar's name area
    falls through to the ordinary frame-label click (select, rename). Below
    240 view px it collapses to the name and a globe; frames under that keep a
    plain label.
  - A tool drag or a click ends on Select with the new frame selected and its
    address open for typing. A click without a drag drops 1280 × 800.
  - An address that isn't http or https keeps the editor open and says so on
    the status line ("Browser View opens http and https pages only."); a bare
    host gets https, and `localhost`, `127.0.0.1` and `[::1]` get http.
  - Back and forward come from `Page.getNavigationHistory`, read after each
    navigation.
  - "Not your site" is `ProjectRegistry::owns`, cached for 2 seconds per
    frame. Live's `isMockup` keeps its own test for now.
  - The sign-in strip shows once in the first frame that is at least 240 × 120
    on screen. The answer is stored in `QSettings` as
    `browserView/signInOffered`. Sign In… opens a normal window on the pool's
    profile with no debugging port (nothing drives it), and the frames show
    "Signing in…" like Live's window until it closes.
  - The bar's right-click menu is mirrored in Object ▸ Browser View, so
    Ctrl+K finds it as "Browser View: …". Set as Design Width came with
    the breakpoints commit (below).
- **The Browse tool (commit 5).**
  - Browse sits in the Selection slot of the rail with Select and Direct
    Select, and has no default key (it is under the tool's flyout and Ctrl+K).
    Esc always returns to Select. Ctrl+K stays the command palette, whatever
    the page is doing.
  - Keys go to a page only after a click has put the focus there. Before
    that, and after a click on empty canvas, the canvas's own keys work
    (tool letters, Space to pan). Focus is dropped when the canvas loses
    focus (a click in a panel ends page typing), when Browse is left and when
    the frame is deleted. Dragging on empty canvas pans, as Figma's hand does.
  - Locked frames can still be browsed; hidden ones can't be reached.
  - A click is `mouseMoved`, `mousePressed`, `mouseReleased`; the click count
    runs across native double-click events (a triple click takes the
    paragraph). A drag holds the left button and ends with the release, also
    when the tool changes or the drag is cancelled. One wheel unit is one
    CSS pixel; Ctrl and Alt with the wheel still zoom the canvas.
  - Enter sends the text `"\r"`; Ctrl shortcuts send Chromium's editing
    `commands` (select all, copy, cut, paste, undo, redo) as well as the key.
    Text from an input method is sent with `Input.insertText`.
  - `BrowserViewHost::dispatch` returns false for a page that can't take the
    input. A frame that reset paused starts again on the click and holds
    nothing until the next one.
  - Popups (`target=_blank`, `window.open`) are found through target
    discovery, closed and loaded in the opener's tab, http and https only. A
    popup that stays blank for 3 seconds is closed. It may begin loading
    before it is closed; that request is accepted.
  - JavaScript dialogs are dismissed (a leave-page prompt is accepted) and
    their text shows once on the status line as "<host> says: …". File
    choosers are refused with "Uploads aren't supported in Browser View yet."
    Fullscreen and pointer lock are refused by a script added to every new
    document. Permissions (location, notifications, camera, microphone and
    the like) are denied for the browser each time a tab opens; a name this
    Chromium lacks is ignored.
  - **Screencast frames.** Chromium's `everyNthFrame` counts frames, not
    changes, so with 2 or 4 it skipped the single frame a click or a load
    made, and the picture stayed on the old state (this was the flaky "no red
    picture" failure of earlier commits). Chromium now sends every frame, and
    the canvas keeps the gap itself: it holds the newest frame until the gap
    since the last decode has passed, so the last frame of a burst is always
    shown. Changing the gap never restarts the cast.
  - Focus emulation (`Emulation.setFocusEmulationEnabled`) was tried so pages
    draw a caret. It starves the screencast, and headless pages already count
    as focused (`document.hasFocus()` is true), so it isn't used.
- **Resize preview and breakpoints (commit 6).**
  - A preview is an `Interaction` flagged `discard`. `commitInteraction` on one
    cancels it, so any edit, undo, lock, page switch or tool change ends it
    without a history step. The exception is Browse: it keeps a held preview,
    so a site can be browsed at its phone width. (Section 6 says "switching
    tools ends it"; browsing at a held width is what the previews are for.)
    `browseBox` reads the document's bounds, so it uses the preview box with no
    extra code.
  - Changing the address during a preview also changes the interaction's saved
    `before` and `base`, so ending the preview doesn't put an old address back.
  - Dragging a handle of one selected Browser View is a "Preview Width" that
    is cancelled on release. Other frames, and a Browser View selected with
    others, scale as before.
  - The breakpoint buttons sit in the bar, right of the name, and show for a
    selected, browsed or hovered frame (the hover reaches 36 px above the
    frame, over the bar). The design width is always among them, dotted; a
    press on it, on the held width again, on Esc or on the canvas elsewhere
    ends the preview. The buttons drop out when the address would keep less
    than 120 px, except while a preview is held (20 px), so a phone-width
    preview keeps its way back.
  - Set as Design Width is in the bar's menu (on a button or while a preview
    shows) and in Object ▸ Browser View (only while a preview is held). It is
    the one "Design Width" step. A frame that isn't a Browser View is left
    alone.
  - "Fixed while previewing" is a checkbox in Layout for the direct children of
    a Browser View. In a preview such a child is made absolute on the preview's
    copy only, so it neither flows nor follows constraints.
  - `Breakpoints` (oma_live) is pure: `scanScript()` and `fromScan()`. The
    scan runs after each load of a frame that is your own site
    (`ProjectRegistry::owns`); its answer is cached per origin. `ProjectRegistry::owns`
    already existed, so `LiveSession::isMockup` is unchanged.
  - `BrowserViewHost::breakpoints` has a default that answers the four
    defaults, so canvas tests need no fake for it.
- **The review round (phase 3 fixes).**
  - **One address rule.** `BrowserAddress::allowed` (http and https only) is
    the only test: the codec, `setBrowserLocation`, the page's own
    navigation and Open in My Chromium all use it. A failed load reports
    `chrome-error://`; the frame keeps `unreachableUrl`, the address that was
    tried.
  - **The design width is `EditorSession::designDocument()`.** During a held
    preview it is the interaction's `before`; save, export, Share, Export for
    Screens and the agent's export all read it, so a phone-width preview is
    never written out as the design.
  - **A held preview and undo.** The first Ctrl+Z ends the held width and
    goes no further; the next one undoes a step. The canvas drops its held
    button when the preview ended for any reason.
  - **The picture is view state.** `setBrowserPicture` (like
    `setBrowserLocation`) writes the same image into every history snapshot
    and the interaction's `before` and `base`, so it is never an undo step and
    a picture that lands during a preview survives it. A rotated or otherwise
    transformed frame draws, exports and stores no picture
    (`VectorObject::showsBrowserPicture`).
  - **Two profile holds.** Live's window and the sign-in window each hold the
    profile; it is busy while either is up, and neither can release the other.
    Sign In… isn't offered or run while Live's window is open. The sign-in
    window is started detached, with its pid (and start time) watched, so
    quitting Omastrator leaves it open.
  - **Cap bursts wait.** Opens past the tab cap (tabs plus tabs being made)
    queue in the pool and go one at a time, so a burst never overshoots the
    cap. The frame that starts the browser waits in the same queue, so a
    close during the start drops it.
  - **Chromium's own blank tab.** The pool asks `Target.getTargets` after the
    start and closes the `about:blank` page Chromium opens by itself, so a
    tab count is the frames' tabs.
  - **Paused means hidden.** A canvas counts as on screen only when it is
    shown and its window is exposed (not minimized, not on another
    workspace). A frame paused for 5 minutes has its tab closed, so the
    60-second idle stop can happen; showing it reopens a tab. The exposure
    half has no automated test: offscreen keeps a minimized window exposed.
  - **Late replies.** `onOpened` ignores a frame that a reset or Live's window
    paused meanwhile. History and breakpoint replies hold a `QPointer` and
    post to the app thread.
  - **The bar works on a locked frame.** Its controls are view state, like
    Browse. Only the address field, which is an undo step, refuses.
  - **Palette shortcuts are read live.** The keys the page never sees are Esc
    and whatever the `commandPalette` action is bound to at that moment
    (Ctrl+K if it can't be found). Leaving Browse sends a keyUp for every key
    still down.
  - **`BrowserViews::notePicture`** is the way a picture enters an entry, and
    tests use it in place of a page. `poolKey` is public so a test can
    deliver a pool signal.
  - **Share and Send to a device** flush the newest pictures first
    (`ShareController::render`).
  - **The breakpoints scan** stops at 2,000 media entries.

## Follow-ups (found in review, not done)

- **`closeAll` is synchronous** when Live opens its window: it blocks the UI
  thread until the browser has stopped. An asynchronous version with a
  `stopped` signal would let the frames show "Live is open" at once.
- **Ten animated frames.** The ack delay was tuned with one or two streams. The
  cost of ten animated frames (decode threads, ack round trips, the 66 ms gap)
  hasn't been measured.
- **`runReset` racing the app's stop.** The command-line reset ends the
  browser after the app's reset, or when the app didn't answer; if the app is
  stopping the pool at the same moment both go for the same pid. The state
  file can't tell the pool's Chromium from Live's on the same profile if a
  pid was reused; checking the pid's start time, or having the app's reply say
  the pool stopped, would close it.
- **Test isolation.** The Chromium tests don't set `HOME`, and Chromium can
  still touch `~/.pki/nssdb` with a custom `--user-data-dir`. The Live tests
  do the same.

## 10. The Browser View switch (2026-09-29)

The author, 2026-09-29 (WINDOW-LAYOUT.md): "The frame tool can just handle the
browser view. There should just be some toggle UI around the frame to turn
Browser View on or off. Turn Browser View on: it spins up the localhost server
with the live code. Turn it off: the localhost server is frozen until Browser
View is back on or until the app is closed. When the app closes, the localhost
shuts down."

**The tools.** There is no Browser View tool. The rail's Frame group is split:
`{frame}` and `{artboard}` (artboards are parked for removal in
backlog/FRAMES-ONLY.md). `Tool::browserView` stays in the enum and in
`toolInfo` so old names still read, but it's out of `allTools` (the rail,
Ctrl+K, the agent's tool list), and `selectTool(Tool::browserView)` picks Frame.
Browse stays in the Selection slot, and Edit Page is still the double-click.

**The model.** `BrowserView::on`, default true. The codec writes
`"browserView": {…, "on": false}` only when it's off, so older files load with
their views on and an older build reads an off view as on.
`VectorObject::showsPage()` is `browser && browser->on`: the bar, streaming,
Browse, Edit Page, the width preview and the sign-in strip all ask it. An off
frame keeps its address and last picture, draws that picture, wears the plain
frame label, and resizes as a plain frame does (a real, undoable resize).

**The switch.**

- A Browser View has it in its bar, after back, forward and reload: a small
  track, always shown (unless the frame is locked).
- Any other frame, and an off Browser View, shows it as a pill at its top-right
  ("Browser View" and the track) while it's selected or under the pointer
  (the innermost frame, its name's row included).
- `EditorSession::setBrowserViewOn(frame, on)` is one undo step, "Turn On
  Browser View" or "Turn Off Browser View". On makes a plain frame a Browser
  View with no page yet, rounded to whole CSS px; off keeps everything.
- A plain frame turned on opens the address field ("Type a URL"), as a new
  Browser View always has. The address says which project runs: an address in
  `projects.json` (This Is My Site…) is the user's own.
- The same switch is Object ▸ Browser View ▸ Turn On/Off Browser View (its
  words follow the state) and in Ctrl+K. Browser View has no agent methods of
  its own; agents reach the switch through the object JSON (`browserView.on`),
  and the server follows as below.

**The server.**

- **On** (the switch, the menu or Ctrl+K: `BrowserViewHost::browserViewSwitched`)
  asks for the page to run. Once the frame has an address and can be seen, an
  own-site, non-local address starts Live on the frame (`LiveFrames::start`),
  which acquires the project's dev server from `DevServers` and moves the tab
  onto it, as the first Edit Page used to. Somebody else's site, or a
  `localhost` page, has nothing of ours to start.
- **Off** freezes it. `LiveSession::setServerPaused` marks its lease frozen;
  `DevServers` freezes a server when every lease on it is frozen (a window's
  Live or another frame on the same project keeps it running), by SIGSTOP to its
  process group, so it keeps its port, state and PID. A static site stops
  answering (`QTcpServer::pauseAccepting`). The tab pauses as an off-screen one
  does, and the frame shows its last picture.
- **On again** wakes the same process (SIGCONT) and the stream. A server frozen
  while it was still starting freezes once it answers.
- **Undo and redo** change only the document. `BrowserViews` follows every
  change of `on` it sees (the switch, undo, redo, an agent) the same way, so
  history can freeze or wake a server but never start a second one or leave one
  behind. Undoing "Turn On" on what was a plain frame removes the Browser View,
  which ends its Live and lets its server go.
- **Opening a file never starts a server** (no `npm install` on open): a view
  saved on shows its page, and its server starts when it's switched on (or on
  the first Edit Page, as before).
- **Quitting** ends every server: `DevServers::stopAll` on `aboutToQuit` wakes a
  frozen group before it sends SIGTERM (SIGTERM, then SIGCONT, then SIGKILL
  after 5 s), and then waits up to 2 s for anything the script started that
  outlived it before killing the group. The headless browser stops with the
  pool.
- **The background app** (`omastrator --daemon`, ANYWHERE.md) keeps running
  when its window closes. To the user the app has closed, so
  `BrowserViews::stopServers()` runs then: every frame's Live ends (its server
  with it) and the headless browser stops. A frame still switched on starts its
  server again when its canvas is next seen. Live's own window keeps its server.
- **Known gap:** if Omastrator crashes while a server is frozen, the server's
  death signal (`PR_SET_PDEATHSIG`) waits until something continues it; `kill
  -CONT` on it lets it end.

**For the island bar** (WINDOW-LAYOUT.md): `BrowserViews::browserViewChanged(frame,
on)` fires on every change by any door, and on a frame first seen on;
`BrowserViews::browserViewOn(frame)` (or `EditorSession::browserViewOn`) and
`BrowserViews::server(frame)` (`none`, `starting`, `running`, `paused`,
`failed`) answer for a selected frame (`EditorSession::selectedFrame()`);
`BrowserViews::setBrowserViewOn(frame, on)` is the island's switch.

**Tests.** `DevServersTests` (freezing a group and waking the same PID, only
when every lease is frozen, freezing a server that's still starting, stopAll
ending frozen and running servers and their helpers, a static site),
`BrowserSwitchTests` (Chromium and python3: on runs one server, off freezes it,
on wakes the same PID, undo and redo, quitting ends it; a file opening starts
none; the last window closing stops them and showing it restarts them),
`BrowserBarTests` (the pill and the bar's switch, undo steps, a locked
document, the old tool name), `DocumentCodecTests` (`on` round-trips, old
files read as on), `MenusTests`, `CommandPaletteTests` and `ContentViewTests`
(the rail).
