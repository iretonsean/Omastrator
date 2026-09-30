# Live inside the frame (design, 2026-09-29)

Phase 4 of BROWSER-FRAMES.md: **the element bar, tokens, Review changes,
History, Deploy and "Build it", inside a Browser View.** Phase 3 is built
(BROWSER-VIEW.md). Removing Live mode from the island is **not** in this phase:
the island, its Live row and Live's own window keep working unchanged
(ANYWHERE.md's interim rule). "The island step" at the end names its seam.
Nothing here is built.

## 1. One Live, two hosts

**A frame host for `LiveSession`.** `LiveSession::Target` gains `frame` (the
pool key) and `pool`. A frame session:

- is made by **`LiveFrames`** (oma_ui, one per `EditorSession` beside
  `BrowserViews`), moved to `pool->poolThread()` and started through
  `BrowserPool::run`;
- launches nothing. Its `cdp()` is `*pool->cdp()`, its page is
  `pool->session(key)`, and its events come from `BrowserPool::tabEvent` for its
  key (direct, same thread). `run` does `Runtime.enable` and `prepare()`,
  evaluates the overlay into the loaded page (as `runInTab` does), then calls
  `pageLoaded()`;
- keeps every `callAndWait` on the pool's thread, which phase 3 allows. The UI
  never calls it directly: `LiveFrames` holds a per-frame snapshot (state,
  message, project, mock-up, URL, selection, hover, pending edits, tokens as
  JSON), refreshed by a queued signal. It sends commands (edit, preview, undo,
  keep, screenshot) through `pool->run`, and answers come back behind a
  `QPointer`;
- **outlives its tab.** An eviction, a 5-minute pause, a lost Chromium or
  Live's window taking the profile closes the tab but not the session. The
  session waits ("Waiting for the page…") and re-attaches on the next `opened`,
  putting its edits back. Only Stop Live, deleting the frame, closing the
  document and reset end it.

**The overlay's frame mode.** `__oma.setHost("frame")` runs before `setTokens`:

- The overlay draws nothing (no layer, bar, strip or notices), so neither the
  screencast nor the stored picture ever shows Live's chrome.
- It still selects, previews, applies, reverts and re-applies.
- It reports `hover` and `select` with each element's rect, once per animation
  frame and on scroll and resize.
- Outside Edit Page (section 3) it is `enable(false)`, so Browse gets the plain
  page.

| Shared (per project or origin) | Per host |
|---|---|
| The registry, the dev server, `AgentBridge`'s reviews, deploy pipeline and History, the edit sets on disk, `deploy.json` | The page and its overlay, the selection, the token scan, the pending edits, Live's undo (section 4) |

- **Dev servers are shared.** `DevServers` (oma_live, app-wide) keeps one
  counted `DevServer` per canonical folder.
  - `acquire(folder, done)` starts it on the dev servers' own thread, since
    `DevServer::start` waits in nested loops for up to 120 s, which must not
    happen on the pool's thread or the UI's.
  - The last `release` stops it.
  - Live's window uses it too (its `run` waits as now), so a window and a
    frame on one project share one server.
- **Pending edits are gathered by project.** `AgentBridge::pendingEdits(folder)`
  reads the window's session, every frame session on that folder, and the
  **held edits**.
  - Held edits are own-site edits from a host that stopped (reset, or the
    document closed), kept per folder for the rest of the app session.
  - Write Back, Save and Deploy take them all, and clear what they wrote from
    each host.
  - Quitting loses held edits, as it loses the window's edits today.

**The profile pause stays, for the window only.**

- Frames never pause for Live in a frame: it runs in the pool's own tab.
- Live's window (from the island or the Live sheet) still stops the pool and
  pauses the frames exactly as phase 3 built it. Frame sessions wait, and
  re-attach when the window closes.
- The island step removes the window, and with it `setLiveOpen`,
  `closeAll(true)` and the "closeAll is synchronous" follow-up.
- Live in the user's own tab (`BrowserLink`) is untouched.

## 2. Starting Live in a frame

**Live starts on the first Edit Page, not when the frame opens.** Viewing a
page costs nothing new, and opening a file never runs `npm install`. Live stays
on after Edit Page ends, so the edits stay visible and deployable.

**Your own site** (`ProjectRegistry::folderFor` of the frame's document URL):

- **A local address** is used as it is (today's rule).
- **Otherwise** `LiveFrames` calls `DevServers::acquire(folder)`, and the frame
  shows `DevServer::step` ("Installing packages…", "Starting pnpm run dev…")
  over its last picture. Then the tab loads the dev server at the same path
  and query.
- **The document keeps the production URL**, so the file still means something
  on another machine:
  - While Live runs, the tab's navigations on the dev origin are written with
    `setBrowserLocation`, with the origin swapped back.
  - Phase 3's rule becomes "a frame's tab shows its document URL, on the dev
    server while Live runs".
  - The bar adds a "dev" tag, whose tooltip is the server's address and
    command.
- **If the server fails,** Live stays off and the frame says "Couldn't start
  the project: <line>", with Details (the server's output).
- **Stopping.** Stop Live, or the session ending, releases the server and loads
  the production URL again.

**A site that isn't yours** keeps today's rules. It is a mock-up: edits stay on
this machine, enabled edit sets are put back on every load, and there's no
Deploy or Save (`liveDeploy` refuses). This shows in four places:

- the bar's "Not your site" tag;
- Keep Edits… in the element bar's ⋯;
- the Live panel's site section, following the frame;
- the bar's menu, which carries the page strip's actions (section 4). The strip
  itself isn't drawn in frame mode.

**This Is My Site…** (a click on the tag, or the menu) opens the Live sheet's
folder chooser (`ProjectRegistry::suggest`). Confirming it registers the
origin, and from the next load the frame is your own site.

## 3. Selecting page elements: Edit Page

**Not Browse, and not a modifier.** Browse stays a plain browser whose clicks
follow links, and Ctrl-click and Alt-click already mean deep select and
duplicate. So element selection is a **per-frame mode, like entering a group:
Edit Page.**

**Ways in:**

- double-click the page with Selection, where no design object is hit;
- the bar's **Edit Page** button (a pencil), shown whenever the breakpoint
  buttons are;
- Object ▸ Browser View ▸ Edit Page, and Ctrl+K;
- the task bar's first action for a selected Browser View.

**While it's on** (`EditorCanvas` holds the frame's id):

- The frame's design children draw at 25% and aren't hit-tested, so the page is
  reachable.
- The pointer goes to the page through `Input.dispatchMouseEvent`, with
  Browse's mapping (CSS px, including the preview box):
  - moving is the overlay's hover;
  - a click selects, and the overlay swallows it, so a link doesn't navigate;
  - Shift-click adds;
  - the wheel scrolls the page.
- The canvas draws the hover (1 px accent) and the selection (2 px, labelled
  with tag and size as Inspect does) from the reported rects, in view space.
- **Keys stay the canvas's.** Nothing is typed into the page:
  - Ctrl+Z is Live's undo (section 4);
  - a tool key leaves Edit Page and picks that tool;
  - Delete does nothing to page elements.
- A held breakpoint preview survives, as it does in Browse, so you can edit at
  390.
- **It ends** on Esc, a click outside the frame, another tool, a page switch,
  deleting the frame, Stop Live or reset. Live stays on.

**The element bar** is `ElementBar`, a widget in src/Canvas like `TaskBar`.

- **Fed through `BrowserViewHost`**, so oma_canvas still doesn't link oma_live:
  `elementBar(frame)` gives the selection and the tokens as JSON, and
  `editElement(frame, property, value, preview)` sends a change.
- **Placed** 8 px below the selected element's box, or above it when there's no
  room, and clamped to the frame's visible rect. It is a constant 28 px on
  screen and follows the zoom and the page's scroll.
- **Several elements:** a change goes to each, and a value that differs shows
  "Mixed".
- **Its controls,** left to right, each only when it applies:
  - **Edit Text**, for elements that hold only text (double-clicking one does
    the same). It opens a `QLineEdit` over the box: Enter commits, Esc cancels.
  - **Colour and background wells:** the page's tokens as swatches, with
    `ColorPickerSheet` one step away.
  - **Padding** as PANELS.md's ↔ ↕ pair, with the box icon for four sides.
  - **W and H**, **font size and weight**, and **radius**.
  - **Ask…**, which is `liveAsk` with the selection.
  - **⋯:** margin, per-corner radius, Copy Selector, Hand to Agent…, and Keep
    Edits… on a site that isn't yours.
- **Fields** are `NumberField`s (24 px, scrubbing). A snapped value shows its
  token as the unit ("p-4", "radius-md"), and a small arrow lists the scale.

## 4. Edits, undo, and where Review, History and Deploy live

**Edits run today's pipeline unchanged:** the overlay's info, then
`TokenSet::resolve` (a Tailwind class swap, a CSS custom property or an Omarchy
colour), then `applyResolved`, then the record.

- Scrubbing sends `__oma.preview` on each step, and releasing makes one
  `edit()`.
- The bar then shows the snapped value.
- Spacing handles on the element are left for later.

**Undo is Live's own history, per frame session. Page edits are never document
undo steps.** Why:

- **The edits aren't in the file.** They live in the page, then in the code or
  an edit set. A document undo couldn't take back a write-back or a deploy, and
  one stack that sometimes reaches into files is worse than two clear ones.
- **The window host can edit the same project** without the document knowing.
- **Phase 3 kept page state out of history** (navigation, scroll, pictures), and
  this follows it.

How it works:

- Each `edit()` pushes the element's `style`, `class` and text from just before,
  plus the record it replaced.
- `LiveSession::undoEdit` and `redoEdit` put them back through a new
  `__oma.restore(selector, state)`.
- In Edit Page, Ctrl+Z and Shift+Ctrl+Z reach it, and Edit ▸ Undo reads "Undo
  Page Edit". Everywhere else, Ctrl+Z is the document's.
- A page edit never marks the file modified.
- **Three layers:** pending edits use this undo, written ones use Review
  changes' Discard, and committed ones use History's Restore. Write-back and
  Keep Edits clear the stack for the edits they take.

**Where they live.** Per PANELS.md, Properties gains no section. These use the
existing floating panels and the frame's bar:

- **Deploy** is a button on the accent colour at the bar's right end, for your
  own site.
  - It shows when the bar's buttons do and there's something to deploy: pending
    edits, or write-backs that aren't deployed yet.
  - During a run the stage replaces it: "Writing…", then "Deploying…", then
    "Live at <host>" for 8 s (click to open it).
  - A failure shows "Deploy failed" (or "Save failed" when the run was a Save),
    and a click opens Details.
  - The first-deploy sheet still asks once.
- **The bar's menu** (also in Object ▸ Browser View, and Ctrl+K as "Browser
  View: …"):
  - Edit Page, Deploy, Save, Review Changes, History, Build It, Build It with a
    Note… and Stop Live;
  - on a site that isn't yours: Keep Edits…, Edit Sets ▸ (checkable), Show
    Original, Export CSS…, Before and After to Desk, Hand to Agent… and This Is
    My Site….
- **Review Changes and History** open the existing Live panel (with the changes
  shown) and Live History panel. They follow `AgentBridge::panelProject()`:
  the frame they were opened for (kept until both panels close), else the
  selected Browser View that runs Live on one of the user's sites, else
  `deployProject()`. `deployProject()` itself is the island's and is as on
  `main`.
- **Deploy and Save** are the bridge's existing pipeline, given the frame's folder
  (`DeployRequest::folder`), writing back `pendingEdits(folder)`.
- The status stream's `live` key stays the window's, so the island is unchanged;
  a Browser View's deploy (`DeployRequest::fromFrame`) doesn't show in `live.deploy`.

## 5. "Build it"

**Where it's offered:**

- **Build It** is a button in the bar, left of Deploy, when the frame has design
  children (anything but its picture) and nothing is being built. It's also in
  the menu and Ctrl+K.
- A click builds at once. **Build It with a Note…** adds a line for the agent.
- On a site that isn't yours, Build It opens the Hand to Agent sheet with the
  same package, and that sheet asks for the folder.

**What's sent.** `AgentBridge::handOff` gains a `frame` field:

- **Only the frame's children:** `mockup.svg`, and `mockup.png` at 2× drawn over
  the page's picture.
- **A screenshot of the page,** the frame's last picture (a tab can't be
  captured; see "Decided while building"), saved as `page.png` in the package
  and named in the prompt.
- **The page's URLs:** the dev server's and the document's production URL.
- **The breakpoint,** such as "Designed at 1280 px wide; the site's breakpoints
  are 768, 1024 and 1280."
- **`selectors.json`:** each lifted child's `liftedFrom`, and every child's box
  in page CSS px (with the frame's scroll added).
- **The frame's pending edits.** They're cleared from the frame, so one review
  holds both.

**Where the result waits.** The existing run handles it: a worktree on its own
branch, headless, `agentDone`, merged around the user's edits, and recorded as a
Review named "Build it: <frame name>".

- The prompt says not to commit, push or deploy.
- The worktree's changes are collected, and its branch is removed.
- Deploy stays the designer's press.

**While it works:**

- Build It becomes "Building with <agent>…", with the tray light's breathing
  accent dot and Stop. The frame stays usable.
- One agent is waited on at a time, as today (`m_waiting`). A second Build It
  says "The agent is still working on <what>."
- When it's done, the dev server reloads the page under the design, and the bar
  says "Built. Review changes" (click to open them) until the next action.
- The design children are left alone.

## 6. Escape hatches

- **Reset** (`design reset`, and so `omastrator reset` and Super+Alt+Escape)
  runs `LiveFrames::stopAll` before `BrowserViews::resetAll`:
  - every frame's Live stops, and its own-site edits become held edits;
  - the dev servers are released, and Edit Page ends;
  - a Build It agent stops, as a reset stops any wait today;
  - nothing restarts by itself.
- **Esc** leaves Edit Page in one press. An open text field takes the first
  Esc.
- **Nothing grabs the keyboard:**
  - Edit Page never sends keys to the page. Only Browse does, after a click, as
    built.
  - The element bar's fields take focus only when clicked, and Enter or Esc
    gives it back to the canvas.
  - Installing and starting a dev server never raises a window.
- **Closing a document** stops its frames' Live the same way: the edits are
  held and the servers released.

## 7. Tests

**The rules:**

- Chromium tests `QSKIP` without `Browser::executable()`.
- They load only the Live fixtures, through `StaticServer` or the vite-tailwind
  fake dev server, and never the network.
- `XDG_*` and `OMASTRATOR_RUNTIME_DIR` are temporary, the pool uses
  `Cache::minimal`, and `HYPRLAND_INSTANCE_SIGNATURE` is unset.
- Deploys push only to local bare repositories and run fake commands. The agent
  is the Live review tests' fake.
- A new fixture, `tests/Live/fixtures/liveframe/`, has a link, a heading that
  holds only text, a padded card with a radius, and CSS custom properties.

**The tests:**

- **`LiveFrameTests`** (Chromium):
  - the session attaches to a pool tab, with replies on the pool's thread;
  - an edit changes the computed style;
  - no Live chrome shows in a screenshot;
  - a reopened tab re-attaches with the edits back;
  - undo and redo round-trip the style, class and text.
- **`DevServersTests`:**
  - two acquires start one process, and the last release stops it;
  - a failed start reports its line;
  - `LiveBrowserTests` still pass.
- **`EditPageTests`** (UI, Chromium):
  - a double-click enters;
  - the hover box matches the element under zoom, scroll and a held preview;
  - a link click selects without navigating, and Shift adds;
  - Esc, a tool key and a click outside each leave;
  - no key event reaches the page;
  - Browse afterwards follows the link.
- **`ElementBarTests`:** placement below, above and clamped; the same height at
  every zoom; Mixed values.
- **`LiveFrameEditTests`** (Chromium):
  - `p-4` becomes `p-3` on vite-tailwind;
  - a scrub records one edit;
  - Ctrl+Z reverts the page in Edit Page and undoes the document outside it;
  - no history step is made, and the file isn't marked modified.
- **Starting:**
  - a registered remote address starts the fake dev server, keeps the document
    URL, and writes navigations back swapped;
  - a site that isn't yours puts its sets back on load and has no Deploy, and
    `liveDeploy` refuses;
  - This Is My Site… registers the origin.
- **`LiveDeployTests`** (extended):
  - window and frame edits on one project reach one commit in a bare
    repository;
  - held edits deploy after a reset;
  - `panelProject` follows the selected frame and `deployProject` doesn't.
- **`BuildItTests`:**
  - the package holds only the frame's children, the screenshot, the URLs, the
    width and `selectors.json`;
  - the result is a Review, and nothing is pushed;
  - Stop ends the run.
- **Reset and close:** every frame's Live stops, the dev servers exit, Edit Page
  ends, and nothing restarts.

## Build plan (each commit shippable)

1. **The frame host:** `Target::frame`, the pool-thread session, re-attaching,
   the overlay's frame mode, `__oma.restore`, Live's undo, and `DevServers`,
   which the window moves onto too. Tests: `LiveFrameTests` and
   `DevServersTests`.
2. **`LiveFrames`:** snapshots and queued commands, `pendingEdits(folder)`, held
   edits, `deployProject()`, and stopping on reset and on close. Tests: the
   bridge tests.
3. **Edit Page:** the mode, its entry points, the input mapping, and the boxes.
   Tests: `EditPageTests`.
4. **The element bar and edits:** `ElementBar`, the host calls, text, scrub
   previews, and Ctrl+Z routing. Tests: `ElementBarTests` and
   `LiveFrameEditTests`.
5. **Starting on each kind of site:** the dev server and the swapped origin,
   the "dev" tag, the menu for a site that isn't yours, and This Is My Site….
   Tests: the starting tests.
6. **Deploy, Save, Review Changes and History from the frame.** Tests:
   `LiveDeployTests`.
7. **Build it:** the button, the note, the package, the working state and Stop.
   Tests: `BuildItTests`.
8. **Docs:**
   - BROWSER-FRAMES.md: phase 4 is built, apart from the island;
   - OS-SUITE.md's component 5, and ANYWHERE.md's escape hatches;
   - AGENTS.md: `DevServers` and `LiveFrames`;
   - the user guide.

## The island step (not in this phase)

The island reaches Live only through `AgentBridge::startLive` (the `live start`
action) and the status stream's `live` key. The step would:

- make `startLive` create a Browser View on the front page and call
  `LiveFrames::start(frame)`;
- have `live` report the frame session (of the selected frame's project, which is `panelProject()`'s, not `deployProject()`'s);
- delete the window path, `BrowserViews::setLiveOpen`, and the pool's
  `closeAll(true)`.

Deploy on the island goes through `deployProject()` and the window's edits;
Changes and History go through `panelProject()` and `pendingEdits(folder)`.

## Decided by the lead (the author can reverse these)

- **Edit Page is its own mode**, entered with a double-click. It isn't a Browse
  modifier.
- **Page edits undo in Live's own history**, not the document's.
- **Live starts on the first Edit Page**, not when the frame opens.
- **The document keeps a site's production URL** while its frame shows the dev
  server.

## Decided while building

- **Commit 1 (the frame host).**
  - The session lives on the pool's thread (`moveToThread(pool->poolThread())`),
    so its synchronous `callAndWait` calls work against the pool's CDP socket.
    Callers run `start`, `stop`, `edit` and the rest there (queued or blocking).
  - The session outlives its tab: `closed` drops the page and sets the state to
    "Waiting for the page…", `opened` attaches again. The edits and the undo
    stack stay, and `pageLoaded()` puts the edits back on every load in a frame.
  - The overlay in a frame is announced by `window.__omaHost = 'frame'` before
    the script. It draws nothing, keeps `enabled` off until `setPageEditing`,
    and reports `{hover, selection, scroll, viewport}` as a `geometry` message,
    once per animation frame while it changes.
  - Undo is per session: 200 steps of `{selector, was, now}` (inline style,
    class, and text for text edits), applied by `__oma.restore`. A new edit
    clears redo; anything that replaces the edit list clears both.
  - `DevServers` is a process-wide manager with leases. `LiveSession::stop`
    releases with `wait = true` in the window (so a stopped Live still means a
    stopped server) and without it in a frame. Frame sessions don't start
    servers until commit 5.
  - A frame's tab can't take `Page.captureScreenshot` ("Not attached to an
    active page"); the frame is seen through a screencast. The no-chrome test
    checks the page's DOM instead, which is what the screencast would carry.

- **Commit 2 (`LiveFrames`).**
  - One `LiveFrames` per `EditorSession` (a child, like `BrowserViews`), with a
    static list for the cross-document calls. The UI never calls a frame's
    session: it reads a `Snapshot` the session publishes from the pool's thread
    after each `changed` or `geometryChanged`, and sends commands with `run`,
    which execute on the pool's thread and answer on the UI thread.
  - `stop` (deleting the frame, closing the document, reset, the pool shutting
    down) ends the session on its own thread and waits, so nothing of it posts
    afterwards. Edits made on a user's own site are then **held** per project
    folder, in memory, until they are written or the app quits. A mock-up's or
    another site's edits are not held.
  - Pending edits of a project are the window's, the frames' and the held ones
    (`AgentBridge::pendingEdits`). Write-back, Ask and Deploy read them all and
    clear all three. Edits left for the agent go back to the window if it is on
    the project, else are held; the Ask that follows takes them.
  - The island's project, `deployProject()`, is as on `main`: the window's
    project if it has one, else the last one the window or the island used
    (`m_lastProject`). Frames never touch it. A frame's project reaches the
    panels through `panelProject()` and the frame's own folder.
  - The agent's brief for a project that is only in a frame has no screenshot
    (the window's session takes those) and carries the frame's address.

- **Commit 3 (Edit Page).**
  - Edit Page is a mode of `EditorCanvas` on the Selection tool (`editPage`), not
    a tool: picking any other tool, Esc, a click outside the frame, deleting the
    frame or leaving its page ends it. Entering deselects the document, so the
    frame's own object is never selected while its elements are.
  - The seam is `BrowserViewHost::beginEditPage / endEditPage / editBoxes`.
    `beginEditPage` starts Live on the frame if it isn't running (with no
    folder yet; commit 5 picks it) and turns `setPageEditing` on. It refuses
    a frame with no address.
  - A press reaches the page as a mouse move then a press, in page CSS points
    (the Browse tool's mapping, so a held breakpoint preview and zoom are
    covered). The overlay decides what it is: it selects, `Shift` adds, and it
    swallows the click, so a link doesn't navigate. No key event goes to the
    page in this mode.
  - The wheel goes to the page and Ctrl+wheel still zooms. Double-click on a
    frame enters; the bar's pencil (after the width buttons, shown wherever
    they fit) and Edit Page in the Browser View menu and the task bar toggle.
  - The frame's children draw at a quarter of their opacity while in the mode
    (direct children only; the renderer compounds opacity down the tree). The
    hover is a 1 px outline, the selection 2 px with a `tag  w × h` pill.
  - The Browse and Edit Page modes share the browse drag, so a drag in Edit Page
    is a page drag that the overlay swallows.

- **Commit 4 (the element bar and edits).**
  - `ElementBar` lives in `oma_canvas`; what is in it comes from
    `UI/ElementBarActions` (which owns `NumberField` and the colour menus),
    attached from `Menus::watchFront` next to `TaskBarActions::attach`. It is a
    28 px plate at every zoom, 8 px from the pick, below it or else above, kept
    inside the frame's visible rect (inside the canvas when the frame is
    narrower than the bar). It shows only in Edit Page with a pick, and hides
    during a page text edit or a gesture.
  - Lengths still snap to the scale (`TokenSet::resolve`), so typing 13 for
    padding on a site with a 24 px gap lands on 24. Properties with no scale
    (font size on a site without font tokens, opacity) go through as typed.
  - A scrub is shown only: `previewSelected` sets inline styles and remembers
    the inline style from before the scrub, and `endPreview` takes them off
    before the one recorded edit, so its "before" is the page's.
  - A change to several properties or elements (padding ↔ is left and right, ↕
    is top and bottom) is one grouped undo step (`UndoStep::group`); undo and
    redo walk the group.
  - After every edit, undo or redo the session re-reads the selection's info, so
    the bar shows what the page has now.
  - In Edit Page, Undo and Redo are the page's ("Undo Page Edit"): they go to
    Live's history through `BrowserViewHost`, never make a document step, and
    never mark the file modified.
  - Edit Text (and a double-click on a picked text-only element) opens a
    `QLineEdit` over the pick: Enter records one text edit, Esc or losing
    focus cancels.
  - The ⋯ menu gains margin, per-corner radius and Keep Edits… in the fix
    round (below). Hand to Agent is Build It.

- **Commit 5 (starting on each kind of site).**
  - The document keeps the production address. When Live serves a registered
    site from its project, the tab shows the same path and query on the dev
    server's origin; `BrowserViews` converts both ways (`toTabUrl`,
    `toDocumentUrl`, a `m_swaps` hash per frame), so typing, the address bar and
    saving never see `localhost:port`. Stopping Live, or a failed start, takes
    the tab back to production.
  - `LiveSession::frameProject` decides the project: on the dev server's own
    origin it is the served folder (nothing is written to the registry);
    otherwise the target folder is remembered for the page's origin, or found
    from the registry. A non-loopback web page with a project starts the
    project's server (`serveProject`, state "Starting the project…"), on the
    pool thread, through the shared `DevServers`.
  - A site that isn't registered never gets a server: Live runs on its own
    page and edits are kept on this machine.
  - A failed start says "Couldn't start the project: <first line of the
    server's output>" as the frame's message; the tab stays on production.
    Choosing Edit Page again stops the failed session and starts a new one.
    The server's full output ("Details") is not shown yet.
  - If the dev server exits while a frame holds it, that frame's Live fails
    with "The project's dev server stopped."
  - The bar shows a small "dev" pill while a swap is active; its tooltip is the
    server's address and command.
  - The "Not your site" tag is clickable and so is This Is My Site… in the
    bar menu: a folder dialog (suggestions from `ProjectRegistry::suggest`, or
    Choose a Folder…) that calls `ProjectRegistry::remember`. The next load
    starts the project's server. Tests answer the dialog with
    `BrowserViews::setFolderChooser`.
  - The bar menu for a site that isn't yours (`BrowserViews+Site.cpp`, through
    `BrowserViewHost::extendBarMenu`): Keep Edits…, Edit Sets ▸ (checkable),
    Show Original, Export CSS…, This Is My Site…. Before and After to Desk and
    Hand to Agent… for frames come with commit 7.

- **Commit 6 (Deploy, Save, Review Changes and History from the frame).**
  - Every frame action goes through the window's `AgentBridge`, the one
    pipeline the Live window uses, given the frame's project
    (`liveDeploy`/`liveSave` take a `folder`). `BrowserViews::setAgent` is
    called from `Menus::watchFront`. No frame code runs its own git or deploy.
  - A frame's project is its Live session's project while Live runs on it,
    otherwise the registry's folder for its address. A frame that isn't the
    user's own has no project: Deploy says "This page isn't one of your
    sites, so there's no code to deploy." and nothing runs.
  - The bar's Deploy pill (far right, hidden when the bar is narrow) shows
    only when there is something to send: pending or held edits for the
    project, or a write-back newer than the project's last successful deploy
    in this session (`m_deployedAt`; it is not read back from history). While
    it runs it says Writing…, Committing…, Creating repository…, Pushing…,
    Deploying…; a result is "Live at <host>" for 8 seconds (click opens it);
    a failure stays as "Deploy failed", or "Save failed" for a Save
    (`DeployState::deploy`), until the next attempt (click opens the log). A
    running or finished deploy shows only on frames of its own project
    (`DeployState::folder`).
  - **A failed Save keeps what it wrote (decided 2026-09-29).** Write-back
    writes the certain edits and records them before the agent is asked for the
    rest. If the run then fails (no agent, the agent's change can't be
    written, the commit fails), nothing is rolled back and no half of the Save
    is committed. The written part is already a Review Changes record with no
    commit, and the next Save or Deploy commits every uncommitted record for
    that folder in one commit (`commitAndShip`). Those edits left the pending
    list when they were written, so they are not written twice. The failure
    message says so (`AgentBridge::uncommittedNote`): "index.html is written but
    not committed; the next Save or Deploy commits it." It names files, not edits
    or records: one write-back record can hold many edits. Tests:
    `aFailedSaveSaysSaveFailedAndLeavesTheWrittenEditsForTheNextSave`,
    `theFailureMessageAgreesWithTheCountAndOnlyNamesWhatIsOnDisk`.
  - The menu (bar menu and Object ▸ Browser View) has Deploy, Save, Review
    Changes, History and Stop Live. Stop Live keeps the frame's edits held
    for the project, as before.
  - Review Changes and History select the frame and open the panels for its
    project (`showLivePanel(true, folder)`, `showHistoryPanel(folder)`), which
    keep it through `panelProject()` until both are closed. (The first version
    moved the island's project with `useProject`; fix round 2 removed it.)
  - Frame edits and the window's edits on one project are written and
    committed together as one commit: `pendingEdits` already merges them.

- **Commit 7 (Build it).**
  - Build It is the same `AgentBridge::handOff` the Live window uses, with a `frame`, a `title` and the frame's own package
    fields; nothing new runs an agent. `BrowserViews+Build.cpp` builds the package, `AgentWork::handoffPrompt` says the
    breakpoints, the production address the dev server stands in for, and the frame's pending edits.
  - The art is only the frame's visible children, copied into a blank document and moved so the frame's corner is the
    origin (page CSS px). `mockup.png` is that art at 2× over the page picture; `mockup.svg` is the same art.
    `selectors.json` has each direct child and each lifted shape with its box, the frame's scroll added.
  - The page picture is the frame's last picture (the screencast frame, else the pool's), saved as `page.png` in the
    package. The first version took `Page.captureScreenshot` asynchronously; a Browser View's tab can't do that ("Not
    attached to an active page"), so that path never ran and was removed in the fix round. Build It is synchronous now.
  - The frame's pending edits go in the package and only the edits that were sent are cleared once the package is handed
    over, so the one review holds both and an edit made meanwhile isn't lost.
  - One agent at a time (`m_waiting`): a second Build It says "The agent is still working on “Build it: <frame>”." Its review is
    named "Build it: <frame name>". A finished build shows "Built. Review changes" until the next Deploy, Save, Review
    Changes or History action, or a click on it; the click opens the review.
  - On a site that isn't yours the same items open the Hand to Agent sheet for the folder (nothing is remembered).
    Build It with a Note… asks one line first; an empty note cancels.
  - Not built: Before and After to Desk from a frame. Hand to Agent from a frame is Build It.
  - Reloading the tab after a build is left to the dev server's own reload.

- **Fix round (review A and B).**
  - **The island is as before.** (Superseded by fix round 2 below: the first
    version let the selected frame decide `deployProject()`.) Every frame
    action passes its folder explicitly.
  - **Dev servers never block the pool.** `serveProject` continues from
    `DevServers::acquire`'s callback. The remaining `callAndWait` paths count
    their nesting, and a session deleted from inside one waits (`deleteWhenIdle`).
    That ends the "nested dev-server waits stack" and "a lease can be dropped"
    follow-ups. A session holds one lease (`m_serverProject` says whose); moving
    to another project or leaving the web lets it go, and a non-web address keeps
    the project it had.
  - **Edits follow their project.** A frame's edits are tagged with the site
    they were made on (`LiveEdit::origin`, with `m_targetOrigin` for the folder
    the frame was opened with). Moving to another project hands the old
    project's edits to `LiveFrames::hold` and clears the edits and undo stacks; a
    mock-up moving to a project clears what it hadn't kept. **Edit sets show
    only for mock-ups**: a site with a project has its edits written back.
  - **Cleared edits stay cleared** until the session has run the clear
    (`Frame::clearing`), so a late snapshot can't bring them back.
  - **A dev address never reaches the document.** The swap is kept until the tab
    leaves the dev origin (`DevSwap::retired`); navigations on the server and
    stopping Live both write production addresses back. `ProjectRegistry`'s
    read-modify-write is under a mutex.
  - **Build It.** A held breakpoint preview is cancelled first, so the design is
    what is built. It builds only the frame's children, and the picture is the
    frame's last one (`page.png`). `Page.captureScreenshot` on a frame's tab never
    worked and is gone from the frame path (the window's `LiveSession::screenshot`
    still uses it). Only the edits it sent are cleared; a still-running agent gives
    the named message from `handOff`.
  - **Edit Page.** Bar pills don't select the frame while Edit Page is on. Edit
    Page is in the bar's right-click menu. Enter and Esc in the element bar's
    fields hand the keys back to the canvas, and the fields take focus by click
    only. There is no Ask… on a site that isn't the user's.
  - **Menus.** Object ▸ Browser View has Keep Edits…, Edit Sets, Show Original,
    Export CSS and This Is My Site… for a site that isn't the user's (and so
    Ctrl+K), and its items follow the frame being edited even when the frame's
    object isn't selected.
  - **The element bar.** Custom… uses the app's `ColorPickerSheet` in a floating
    panel, with its opacity field on (`withAlpha`; the picker's other callers stay
    opaque), so a translucent colour is written as `rgba(r, g, b, a)`. The box icon swaps
    the padding pair for four sides. The ⋯ menu holds margin (↔ ↕), per-corner
    radius and Keep Edits… (a site that isn't yours). The overlay now reports the
    four corner radii.
  - **Not observable from a test:** B3 (a frame failing while the window is
    running), which needs a Live window and a frame at once in CI.

- **Fix round 2.**
  - **A start that ends mid-way ends.** Letting go of the dev server while it
    starts drops its answer, so `frameProject` now ends the start itself
    (`m_serving` false, back to `running` or on to the next server) instead of
    leaving "Starting the project…" for ever. A page the frame goes to during a
    start is looked at too (`Page.loadEventFired` while serving), so a
    redirect or a link to another site ends the start at once.
  - **The island exactly as on `main`.** `deployProject()` is the window's
    project if non-empty, else `m_lastProject`. Only the island's and the
    window's deploys set `m_lastProject`; a frame's (`fromFrame`) never do.
    `liveWriteBack` with no folder (the island's and the design controller's)
    writes `m_live.edits()` and clears only the window's; frames always pass
    their folder and get `pendingEdits(folder)`. `live.deploy` in the status
    stream is idle for a deploy a frame started; the frame's bar and the panels
    read `deployState()` directly. The panels follow `panelProject()` (the
    project of the frame that opened them, else the selected frame's, else
    `deployProject()`), and their Deploy passes `fromFrame` when that isn't the
    island's project. Tests: `theIslandsDeployWritesBackOnlyTheWindowsEditsAndKeepsAFramesHeldOnes`
    (Chromium), `aFramesDeployIsNotTheIslandsAndLeavesItsProjectAlone`,
    `theIslandsDeployWritesBackOnlyWhatTheWindowEditedNotAFramesHeldEdits`.
  - **Small.** The handed-off edits of a closing document are held through
    `qApp`'s context, so they survive the document. `m_targetOrigin` is the
    origin of the frame's document address, passed in `Target::url` at start
    (a frame with no web address falls back to the first web page it shows).
  - **Flaky tests.** The registry file is reset in `init()`, pending edits are
    cleared per project folder in `cleanup()` (`clearPending("")` clears
    nothing), the paused-frame test waits for the close, and the "stopping is
    quick" test waits on a condition instead of the wall clock.
  - **The picture follows the page.** Live never stopped the stream: Chromium
    sends no `Page.screencastFrame` after `Emulation.setDeviceMetricsOverride`
    changes the page's size, so a resize (a drag, a held breakpoint) left the
    old picture. `sync()` now stops and starts the screencast again after a
    change of the frame's CSS size, at the size it was casting at until the new
    one settles; that yields the reflowed page at once. A live picture's
    `devicePixelRatio` is its pixels per CSS px, so its
    `deviceIndependentSize()` is the page size it depicts. Tests:
    `aFramesPictureKeepsFollowingThePageOnceLiveRuns`,
    `aFramesPictureFollowsAnEditMadeThroughEditPage` (Chromium).
  - **A stale picture is never distorted.** The renderer stretches a live
    picture over the frame only when it depicts the frame's size (within 2 px);
    otherwise it draws it 1:1 in CSS px, top-left, clipped to the frame, until
    a picture of the new size arrives. The stored picture still stretches.
    Tests: `thePictureDuringAWidthPreviewIsTheReflowedPage` (Chromium: at 390
    the drawn pixels are the narrow layout's), `aStalePictureInANarrowerBoxIsClippedNotSqueezed`,
    `aStalePictureInAWiderBoxKeepsItsSizeAndTopLeftCorner`,
    `aPictureThatShowsTheFramesSizeFillsItWhateverItsDensity`.

- **The stale picture, found (phase 4 picture work).** The report: once Live
  ran in a frame, edits and Build It reached the DOM, but the canvas kept the
  old picture, even after Reload. Neither Live nor the dev server caused it.
  The cause was a frame that had been paused before:
  - **What pausing does:** `pause()` sends `Page.setWebLifecycleState frozen`,
    and Chromium hides the page when it freezes it. `active` unfreezes it but
    leaves it hidden, and a hidden page paints no screencast frames (one on
    start, then none). Chromium 152 shows this with no Omastrator code in
    between: after `active`, `document.visibilityState` stays `hidden`.
  - **Why the report saw it after Edit Page:** its session added a page and
    undid it (a pause and resume) before the resize and Edit Page. The session
    that couldn't reproduce it never paused its frame.
  - **The fix:** resuming also sends `Emulation.setFocusEmulationEnabled
    true`, which shows the page. The pause turns it off before freezing,
    because a page that emulates focus stays visible and its scripts keep
    running while "frozen".
  - **What was driven:** the animation's capture program (the real window,
    offscreen), without and with the fix. Without it, the picture stayed the
    same through the resize drag, the radius scrub, Build It, Reload and
    Deploy. With it, every step reached the picture, including the agent's
    change after Reload. The same program without the page step followed
    every step before the fix.
  - **Also checked:** the dev-server path (a registered production address,
    so `LiveFrames` calls `DevServers::acquire`, and the dev command is
    Python's static server). There the picture follows the move to the
    server, a text edit, a style edit, a stylesheet the agent wrote (a hot
    reload stand-in), Reload and a resize to 390, and keeps following after
    all of them.
  - **Tests:** `LiveFramePictureTests` (Chromium):
    `aFramesPictureFollowsThePageAfterItWasPaused`, and
    `aFramesPictureFollowsThePageOnItsDevServer`, which pauses the frame
    before and during Live and checks each step by the pixels the canvas
    draws. Both fail with the resume's focus emulation removed.
    `VectorRendererTests`: `atAPhonesWidthTheFrameDrawsThePhoneLayoutWhole`,
    and the two stale-picture tests now check all four corners of a
    2-px-per-CSS-px page, at zoom 1 and 2.

## Clearing what was written or sent

`LiveFrames::clearPending(folder, taken)` removes the edits a write-back or a
Build It took, by identity (`LiveEdit` compares every field), from the held
edits and from each frame's session (`LiveSession::removeEdits`, run on the
pool's thread). An edit a session recorded after the caller read the list stays,
published or not; it used to be wiped by `setEdits({})`. A frame's snapshot
leaves out the edits of a clear that hasn't run yet, so the panel never shows
them again in between. Callers: `liveWriteBack` (everything it read),
`liveAsk` (the brief's edits) and Build It (the package's `pending`).
`clearPending(folder)` alone clears what is pending now. Tests:
`clearingWhatWasReadKeepsAnEditTheSessionHadNotPublished` (Chromium),
`clearingWhatWasReadLeavesHeldEditsThatCameLater`,
`removingEditsForgetsExactlyTheNamedOnes`.

## Follow-ups

- Cross-origin undo in a mock-up: two mock-up origins in one session share the
  undo stack (the project doesn't move), so Ctrl+Z on site Y can undo an edit
  from site X by running its restore on Y's element with the same selector
  (`undoStep`). It changes only the page shown, never code.
- The retired dev swap can linger after Stop Live: if the tab has no events then
  (paused or closed), the swap keeps translating until the next non-dev
  navigation, and a deliberate trip to that localhost port meanwhile is saved
  as the production address.
- A resumed frame's page reports `document.hasFocus()` as true, because the resume
  turns focus emulation on to show the page (see "The stale picture, found"). An
  element that has focus (an autofocused field, say) then shows its `:focus`
  styles and caret in the picture, though the user hasn't clicked into the page.
  The pause turns emulation off again, but a frame that stays on screen keeps it.
  Showing the page without claiming focus needs another way to keep Chromium
  from hiding it.
- The token as the unit ("p-4", "radius-md") and the small arrow that lists the
  scale, in the bar's fields. The host's element state doesn't carry an edit's
  snapped token yet; it needs to.
- The server's full output ("Details") for a failed dev-server start.
- Before and After to Desk from a frame.
- Spacing handles on the element.
- The island step above.
