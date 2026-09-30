# Canvas workspaces (design 2026-09-28, built 2026-09-29)

Phase 2 of BROWSER-FRAMES.md: **a document's pages claim Hyprland named
workspaces, and closing the document gives them back.** Phase 1 (PAGES.md)
made page ids permanent and names unique for this. **Built** on `feat/workspaces`;
section 7 records what was decided while building it.

## 1. Opt-in, off by default

- **View ▸ Pages as Workspaces**, also in Ctrl+K. It's one app-wide checkable
  command, stored as QSettings `view/pageWorkspaces`, like View ▸ Contextual
  Task Bar. **It's off by default** during the alpha. It moves the user's
  windows around, and our alpha tester has already had their keys broken by
  setup once. Nothing is claimed until they ask for it.
- **Only documents with two or more pages claim workspaces.** A one-page
  document stays a tab where the user opened it. The second page is the
  moment the document spreads out. Removing pages back down to one gives
  every workspace back.
- **Nothing is written to the user's config.** No setup step, no key file
  lines and no `hyprctl keyword` window rules. Everything is runtime
  dispatches, so there's nothing to break at reload and nothing to back up.
- **Off Hyprland it's a no-op.** The command is greyed out, with the tooltip
  "Needs Hyprland", when `HYPRLAND_INSTANCE_SIGNATURE` is unset and there's
  no `OMASTRATOR_HYPRCTL`, or when the first `workspaces` query fails. If
  Hyprland goes away mid-session (the event socket closes), management stops
  quietly, and the app keeps working as today with its tabs and one window.
  The setting stays on for next time.
- **`--daemon` with the window hidden claims nothing.** Claims start when
  `show_window` shows the window, and hiding it (the daemon's close) gives
  everything back.

## 2. The mapping: one editor window, plus stand-ins

The app has one `QMainWindow` showing one `ContentView`, and the current page
is session-wide view state. A full editor window per page would mean
per-canvas current pages and duplicated panels, all to show pages nobody is
looking at. And a single window that just moves doesn't work either: an
empty named workspace is destroyed, so Super+Tab and swipes could never
reach the other pages. So:

- **The editor window** sits on the current page's workspace.
- **Every other page gets a stand-in.** A stand-in is a chrome-less
  top-level `QWidget` (same app id as the editor)
  that paints a picture of that page:
  - the editor's `grab()` taken when the page was last left, so arriving
    looks seamless;
  - else the page rendered through `VectorRenderer`, fitted to the window.

  Stand-ins exist only to keep workspaces alive and reachable. Clicks and
  keys on a stand-in do nothing, because arriving on the workspace has
  already swapped the editor in (section 4).
- **The swap.** A **spare stand-in** waits on the special workspace
  `special:omastrator-spare` (never shown unless toggled, and not in
  Super+Tab's order). When page N becomes current:
  1. The spare moves to the old page's workspace, so it is never empty.
  2. The editor moves to N's workspace.
  3. N's old stand-in moves to the special workspace and is the new spare.

  Every workspace holds a window at every step, so a named workspace
  keeps its id through any number of swaps. Stand-ins are a pool, not one
  per page id: the app keeps `address → page` in memory, and repaints a
  stand-in with its page's grab when it takes a workspace. Giving back
  closes the spare too.
- **Every open document with workspaces** has stand-ins for all its pages
  except the one on screen. Arriving on another document's page selects that
  tab first. The cap is 24 claimed workspaces in total. Past that, a page has
  no workspace (it's still in the Pages list), and the status bar says so
  once.
- **Placement.** Wayland doesn't let an app place its own windows, so every
  window maps on the focused workspace. The app learns its address from
  `openwindow>>` on the event socket: a new stand-in is first titled
  `omastrator-standin-<n>`, then retitled once its address is known. The
  editor's address comes from `clients`, matched by our pid and exact
  title. Then the app moves it. A new stand-in may tile beside the editor
  for about one frame; that's accepted. No fullscreen state is set: a lone
  tiled window already fills the workspace, and the Omarchy bar stays visible.
- **Dispatchers**, all through `Hyprland::dispatch(lua, legacy)`, and new
  helpers in `Hyprland.h` that build both forms:

  | Helper | Lua (`hyprctl eval`) | hyprlang (`hyprctl dispatch`) |
  |---|---|---|
  | `moveWindow(addr, ws, follow)` | `hl.dispatch(hl.dsp.window.move({ workspace = "name:<ws>", follow = <bool>, window = "address:<addr>" }))` | `movetoworkspace[silent] name:<ws>,address:<addr>` |
  | `focusWorkspace(ws)` | `hl.dispatch(hl.dsp.focus({ workspace = "name:<ws>" }))` (as the Desk does now) | `workspace name:<ws>` |

  The builder checks the name of `window.move`'s window-selector key against
  Hyprland 0.56's Lua dispatcher reference; the rest is already used in the
  code. **Renames never use `renameworkspace`.** A rename moves the
  workspace's windows to the new name, the old one empties and Hyprland
  deletes it, and the focused workspace follows if it was that one. So only
  the two dispatchers above are ever needed.
- **Naming.** `design:<document> · <page>`, built by one function,
  `PageWorkspaces::name(document, page)`:
  - `<document>` is the file's base name without the extension, or the
    tab's title if unsaved ("Untitled").
  - Both parts drop `,` `"` `\` and control characters, since commas split
    dispatcher arguments and the others break the Lua string. Whitespace
    collapses to single spaces, because legacy `dispatch` splits on spaces
    and hyprctl rejoins them with one. Both parts are trimmed.
  - Each part is cut to 32 characters, with "…".
  - Two open documents with the same name: the later-claimed one becomes
    `<document> (2)`, and keeps that suffix while open.
  - Two page names that clash after cutting: the later one gets ` (2)`.
  - A workspace of that name that already holds windows we didn't put there
    also gets a suffix.
  - The name is only ever *derived*. Claims are keyed by `(tab id, page
    id)`, so save-as, a document rename and a page rename are all renames
    as above.

## 3. Lifecycle

- **Claims file:** `<runtime dir>/workspaces.json`, where the runtime dir is
  `$OMASTRATOR_RUNTIME_DIR` or `$XDG_RUNTIME_DIR/omastrator`. It holds the
  Hyprland instance signature, the app's pid, the **return workspace** (the
  one focused when the first claim was made), and each claim: its name,
  tab id, page id, and our window addresses. It's rewritten on every change.
  It lives in a small class in `oma_agent`, `WorkspaceClaims`, so the CLI
  can use it without the GUI.
- **"Give back" a workspace** means:
  1. Close its stand-in.
  2. Move any window we didn't put there (one the user dragged in) to the
     return workspace with `moveWindow(…, follow = false)`.
  3. Drop the claim.

  Once it's empty and unfocused, Hyprland deletes the named workspace itself.
  If the user is on it, the editor or the user's own windows go with them,
  as below.

| Event | What happens |
|---|---|
| Toggle on | Every open document with ≥2 pages claims. The editor moves to the front page's workspace, following only if an Omastrator window has focus. |
| Toggle off | Everything is given back. The editor moves to the return workspace, following if it has focus. |
| New Page / Duplicate Page | Claim; the new page is current, so it swaps. Undo gives it back. |
| Rename page, save-as, document renamed | Rename (section 2). |
| Delete page | Its window is freed (a stand-in closes; if it was current, the swap brings the next page's). Its workspace is given back. Undo reclaims. |
| Reorder pages | Nothing in Hyprland. Super+Tab order is Hyprland's (creation order); the in-app Next/Previous Page follows the Pages list. |
| Down to one page | Everything for that document is given back, and the editor goes to the return workspace. |
| Open a file with ≥2 pages | Claims, as with the toggle. With `show_window` and `raise=false`, nothing follows. |
| Close a document | Its workspaces are given back. The next front tab's page workspace, or the return workspace, gets the editor. |
| Quit, or the daemon hides the window | Everything is given back; the file is emptied. |
| Crash | Our windows vanish, and empty named workspaces go with them. The user's dragged-in windows are left on `design:…` workspaces until the next start or `omastrator reset`. |
| App start (daemon restart included) | If the claims file names another pid that isn't running, or another Hyprland signature, give those workspaces back from the file, then empty it. |
| `omastrator reset`, Super+Alt+Escape | `runReset` gives back everything in the claims file **itself** (no app needed; the fake-able `clients` query plus moves), after the island and the keys, and before calling the app. The app's `design reset` then **turns Pages as Workspaces off** (saved) and says "Pages as Workspaces is off. Turn it on again from View." A reset must not be undone a second later by a reclaim. |

## 4. Focus and navigation

- **The event listener.** `HyprlandEvents`, a new class in `oma_agent`,
  reads `$XDG_RUNTIME_DIR/hypr/$SIG/.socket2.sock`, or
  `$OMASTRATOR_HYPRLAND_EVENTS` (a socket path) in tests. It's a
  `QLocalSocket` that splits the stream into lines with a pure `parse(line)`
  function. It uses `workspacev2>>ID,NAME`, `openwindow>>`,
  `closewindow>>`, `movewindowv2>>` and `destroyworkspacev2>>`, and ignores
  the rest. It only runs while claims exist.
- **Hyprland → app.** `workspacev2` to a claimed name:
  - select that tab if needed;
  - `setCurrentPage(page)`;
  - swap.

  It's coalesced over 50 ms, so fast Super+Tab runs act only on where the
  user lands. The editor paints the new page (`repaint()`, before the move)
  so the frame it arrives in shows the right page and not the one it left.
  The user lands on the page's stand-in first, and Hyprland fades it out as
  the editor fades in; the app can't hide that, since it is Hyprland's own
  window animation and the user is looking at the workspace when it swaps. A
  bar click (section 5) has neither, because the swap is made before the
  user arrives.
- **App → Hyprland.** A current-page change from the Pages list, Next or
  Previous Page, New Page, or undo:
  - swaps with the editor move `follow = true`, **only if an Omastrator
    window has focus when the move is made** (not just when it was asked
    for; a request that finds the app unfocused is dropped);
  - otherwise (the agent's `page` tool, a background open, `raise=false`),
    the moves are silent. A page never takes the user's workspace or the
    keyboard from another app.
- **No feedback loops.** Both paths end in the same idempotent step: "put
  the editor on the current page's workspace and a stand-in on each other
  claimed one". It dispatches only for windows not already where they
  belong, per the last `openwindow`/`movewindowv2` or a `clients` read. So
  our own `focusWorkspace` produces a `workspacev2` that finds the page
  already current and does nothing.
- **The editor is focused** (`focuswindow address:`) only on arrival, and
  only if the active workspace is still that page's when the coalesced
  handler runs.
- **Super+number always leaves.** A numbered workspace isn't ours, so
  `workspacev2` there does nothing, and nothing follows the user out. We
  never bind a key; Super+Tab, `previous` and swipes behave as Hyprland
  orders workspaces. On a live desktop, the builder checks by hand that
  Omarchy's `e+1` reaches named workspaces (they get negative ids). If it
  doesn't, BROWSER-FRAMES.md is corrected: pages are reached from the
  Pages list and Next/Previous Page, and Super+number still leaves.
- **Windows the user drags** onto a page workspace stay there while it's
  claimed, and go to the return workspace when it's given back. If the user
  drags the editor or a stand-in elsewhere, the next swap puts it back.
- **A stand-in the user closes** (Super+W) stays closed: that page's
  workspace is given back, and the page is reclaimed only when the user next
  goes to it from the Pages list or Next/Previous Page. We never reopen a
  window the user just closed.

## 5. The island and the bar

- **The island is gone (2026-09-29).** It was unchanged by this feature: it
  showed while an Omastrator window was focused, and stand-ins and the editor
  both carry our app id. The desktop island was removed with the window-layout
  work (OS-SUITE.md, component 1), so nothing here needs it; the tray light in
  the bar and desktop notifications remain.
- **The design-mode floating bar.** Its home is a window (ANYWHERE.md, "The
  floating bar sticks to its app"):
  - **Stand-ins are never a home.** `DesignController` hands `DesignMode`
    the set of stand-in addresses to skip. If design mode starts with a
    stand-in focused, the home is the editor.
  - Since the editor moves to whichever page workspace the user is on, the
    existing "follows its window when it moves" rule keeps the bar with the
    canvas across pages.
  - On a numbered workspace (the editor off screen), the bar hides, as it
    does now. The proposal card covers a waiting Keep or Discard.

### In the bar

Omarchy's bar lists only workspaces 1 to 10, and a named workspace has a negative id, so the pages never show
there. The plugin `omastrator.pages` (`shell/omastrator.pages`, a `bar-widget`) adds one button for each claimed
page, in page order, right after Omarchy's workspace numbers.

- **Looks like the numbers.** It uses `BarWidget` and `WidgetButton` with the sizes, spacing and focused glyph (`󱓻`)
  of Omarchy's `Workspaces.qml`. An unfocused page is a dot (`•`) at half opacity. A gap a little wider than the
  numbers' own sits before the first page. The tooltip is the page's name, and "Page (Document)" when two open
  documents claim workspaces. With no claims it has zero width.
- **Click** runs `omastrator island page <name>` (`Quickshell.execDetached` on `sh -c`, the name an argument, so it
  needs no quoting). The running app makes that page current and takes the user to it in the same one-batch swap as
  Alt+PageDown, though no Omastrator window has focus: a bar click is an explicit request to go there, so nothing
  shows the page's stand-in on the way (`PageWorkspaces::goTo`, the `go_to_page` method). When the command fails (no
  app, Pages as Workspaces off, an unknown name) the same `sh` line runs Hyprland's own focus as before,
  `hl.dsp.focus({ workspace = "name:<name>" })` with the name escaped for Lua; the app then swaps the editor in
  from the event (section 4, and it flickers as described there). On the page that is already current it moves no
  window and only focuses the workspace, if the user is not on it.
- **Data.** `omastrator status --follow` carries `pageWorkspaces: [{name, page, document}]`, read from
  `workspaces.json` in claim (page) order, and empty while no app is running (a file left by a crashed app is not
  shown). The claims file records each claim's `document` and `pageName` for this; a file from before has them cut
  from the workspace name. The stream reprints when the file changes. The focused button comes from
  `Hyprland.focusedWorkspace.name`, so the widget never queries `hyprctl`.
- **Setup** puts `omastrator.pages` in the bar layout right after `omarchy.workspaces`, in the section that holds
  it, as a step of its own (with a backup, and undone by `--remove`). A second run changes nothing, and a machine set
  up before this step gets it on the next `omastrator setup`. With no `omarchy.workspaces` it goes at the end of the
  left section and setup says so. With no bar layout in `shell.json` at all it adds nothing, because the shell then
  uses its own default layout and a new section would replace it.

## 6. Tests

All tests run in a temporary `HOME` and runtime dir, with
`HYPRLAND_INSTANCE_SIGNATURE` unset. `OMASTRATOR_HYPRCTL` is a script that
logs its arguments and answers `workspaces`, `activeworkspace` and `clients`
from JSON files the test edits. `OMASTRATOR_HYPRLAND_EVENTS` points at a
`QLocalServer` in the runtime dir that the test writes event lines to.
`FakeDesktop` is used wherever a `DesignController` is built. Nothing
touches the real Hyprland.

- `HyprlandEventsTests`:
  - `parse` on every event we use, plus names with `,` `>>` `·` and
    unicode;
  - partial lines across reads;
  - the socket closing ends management without error.
- `PageWorkspacesTests`:
  - naming: sanitizing, cutting, the `(2)` suffixes, unsaved;
  - dispatch strings for Lua (a temp `hyprland.lua`) and hyprlang (without
    one), with the exact log lines;
  - claim and give back on toggle, New Page, delete, undo, down to one
    page, close, and quit;
  - rename by moving;
  - foreign windows go to the return workspace;
  - the cap of 24;
  - off-Hyprland greying.
- `PageWorkspacesSyncTests`:
  - a `workspacev2` line makes the page current and swaps;
  - a burst of three coalesces to the last;
  - the canvas paints the new page before the editor's move (a paint noted in the fake hyprctl's log ahead of the move);
  - `go_to_page` follows the user with no focus in one call to Hyprland, moves nothing for the current page, and
    refuses an unknown name or the feature off;
  - a numbered workspace does nothing;
  - our own echo dispatches nothing (the log is unchanged);
  - the agent's `page add` moves silently;
  - closing a stand-in gives its workspace back and nothing reopens it;
  - no `focuswindow` when the app isn't focused.
- `WorkspaceClaimsTests`:
  - the file round-trips;
  - `runReset` gives back from the file with no app running;
  - startup cleans up a dead pid or an old signature;
  - the app's `design reset` turns the setting off.
- `DesignModeUiTests` (added): a stand-in is never the bar's home.

## Build plan (each commit shippable; the feature is off by default until the toggle lands)

1. `Hyprland::moveWindow`/`focusWorkspace` (both dialects), and
   `HyprlandEvents` with `OMASTRATOR_HYPRLAND_EVENTS`, plus their tests. Add
   the variable to AGENTS.md's list.
2. `WorkspaceClaims` (the file, give back from the file), with the
   `runReset` step and startup cleanup, plus tests. It does nothing while
   nothing claims.
3. `PageWorkspaces` and the stand-ins in `oma_ui`: naming, claim and give
   back across the lifecycle table, and View ▸ Pages as Workspaces with its
   Hyprland gate, plus `PageWorkspacesTests`.
4. Both-way sync: the event handler, focus rules, coalescing and the
   idempotent placement step, plus `PageWorkspacesSyncTests`.
5. Design mode: stand-ins aren't homes, and `design reset` turns the setting
   off, plus tests.
6. Docs:
   - BROWSER-FRAMES.md: phase 2 built; the Super+Tab check's result.
   - ANYWHERE.md, escape hatches: reset gives workspaces back.
   - The user guide's View entry.

## 7. As built (2026-09-29)

Code: `Hyprland` (`moveWindow`, `focusWorkspace`, `focusWindow`),
`HyprlandEvents`, `WorkspaceClaims` (all `oma_agent`), and in `oma_ui`
`PageWorkspaces` (claims and naming; `+Place.cpp` places windows;
`+Sync.cpp` handles the user landing on a workspace) and `PageStandIn`.
The fake Hyprland behind the tests is `tests/UI/FakeHyprlandWorld.h`, on
top of `tests/Agent/FakeHyprctl.h`. Decisions made while building:

- **Placement is one idempotent step, `place()`.** It reads `clients`, then
  dispatches only what is not already where it belongs, so our own move
  events cause no loop. Moves are ordered so a named workspace is never
  emptied (Hyprland deletes an empty, unfocused workspace): a swap goes
  through the spare, so every workspace keeps its id. A brand-new page's
  workspace is the only one made from nothing. Stand-ins nobody needs are
  deleted only after the moves, since one may be the last window on the
  workspace the editor is joining (another document's page, or the page
  after a deleted one). A switch made in the ~100 ms before a new spare has
  mapped still takes the old path and can renew one id.
- **The spare is a real window.** Window lists that show every client
  (switchers, design mode's surface list) show it as "Spare — Omastrator".
  Stand-ins still map tiled beside the editor, holding the keyboard for
  roughly 50–100 ms, until they're placed; turning the feature on makes two.
- **Stand-ins are made only while an Omastrator window is active.** A new
  window maps on the focused workspace and takes focus, so one made while
  the user is in another app would land on their workspace. Without focus
  the needy workspaces wait, and are placed when an Omastrator window
  becomes active.
- **The return workspace is kept as id and name.** A numbered one is
  selected by number (`name:1` makes a new workspace named "1" when 1 is
  gone). The editor goes back to the workspace it was on (never a special
  one, such as the Desk's; then it goes to the focused one), the user's
  dragged-in windows to the one that was focused. Old `workspaces.json`
  files, with no id, still load.
- **Windows are recognised** by the address we last saw, then for a
  stand-in by its first title (`omastrator-standin-N`), and for the editor
  by its exact title, else as the only other window of our pid. A window
  Wayland hasn't mapped yet has no address, so placement retries every
  100 ms, up to 20 times.
- **The stand-in's picture** is the editor's grab (at most 1280 px wide)
  taken as the page is left; a page never left is rendered through
  `VectorRenderer` (at most 1024 px).
- **The cap of 24 always keeps a slot for the front page**, so the editor
  never loses its workspace to other documents' pages. The one-time notice
  reads "Only 24 pages get workspaces; the rest are in the Pages list."
- **The focus rule.** A move that follows the user, and `focuswindow`, only
  happen when an Omastrator window is active (`QApplication::activeWindow()`;
  tests replace the probe). The agent's `page` tool goes through the same
  session calls as the UI, so it is silent exactly when the app isn't the
  active window, which is when an agent is at work from a terminal.
- **Hyprland to app.** Events within 50 ms act once, on the last. A page the
  user walked to is already on screen, so the editor is moved silently and
  then focused, and only if the active workspace is still that page's.
- **A refused move stops it.** The first `moveWindow` Hyprland refuses ends
  the feature for this run: it gives back what it can, drops its stand-ins,
  and says once "Pages as Workspaces stopped: Hyprland refused a move." It
  starts again when reachability is re-checked (turning it off and on).
- **No event stream, no claims.** If the event socket can't be reached when
  the first workspace would be claimed, nothing is claimed and the notice
  reads "Pages as Workspaces needs Hyprland." once.
- **Events are filtered.** Opening, closing or moving someone else's window
  doesn't place anything; only our editor or stand-ins (by address, or a
  stand-in's first title while its address is unknown), and windows moved
  onto a workspace we claim, do.
- **Stale claims files.** At start, a file whose pid is running is left alone
  only if that process has our name (`/proc/<pid>/comm`); a reused pid is
  treated as dead. Stand-ins don't keep the app alive (`WA_QuitOnClose` off).
- **Closing a stand-in** declines that page's claim until the page is made
  current again (Pages list, Next or Previous Page, undo). The front page is
  never declined.
- **`design reset`** says "Reset. Pages as Workspaces is off. Turn it on
  again from View." when the setting was on, and the usual message
  otherwise. Its test is in `DesignModeUiTests`, since it needs a
  `DesignController`; `WorkspaceClaimsTests` covers what `runReset` does.
- **Dispatch strings** (Lua and hyprlang) are tested in `HyprlandEventsTests`
  and `PageWorkspacesTests`. **Unverified:** the Lua `hl.dsp.window.move` key
  names `workspace`, `follow` and `window` are known only from strings in
  Hyprland 0.56.2's binary, not from a running Lua config. The hyprlang
  forms are the documented ones.
- **Design mode.** `PageWorkspaces::allStandInAddresses()` is what
  `DesignController::homeOnFocus` skips; with a stand-in focused, the home
  is the editor.

### First live run (2026-09-29, Hyprland 0.56.2, Omarchy's Lua config)

Run on a headless output with a three-page document:

- **Claims, swaps, renames, New Page and quitting work.** The Lua `window.move` and `focus` dispatchers work as
  built (the key names are right). The editor follows Alt+PageDown and Alt+PageUp, and a workspace the user
  walks to swaps the editor in within about 0.2 s. Quitting gives every workspace back and empties the file.
- **Super+Tab goes through the pages in reverse.** Omarchy's Super+Tab is `focus({ workspace = "e+1" })`, which
  goes by workspace id, and Hyprland gives each new named workspace a lower id than the last. So from the
  first page Super+Tab goes to the numbered workspaces, and Super+Shift+Tab goes forward through the pages.
  Not fixed: a page added later would still come first. Next/Previous Page (Alt+PageDown, Alt+PageUp) go in page order.
- **Fixed: the flicker on every swap and on New Page** (`fix/workspace-swap-flicker`). A swap's moves went to
  Hyprland one call at a time, and Hyprland drew the frame between them: the spare tiled beside the editor on
  the page being left, and the editor, half its width, animated back to full on the next page (about 0.5 s).
  A new stand-in did the same where it mapped. Now:
  - `place()` sends a swap's moves in one call (`Hyprland::dispatchAll`: one `hyprctl eval`, or
    `hyprctl --batch`, split into single calls when a name holds `;`). The order is unchanged, so no
    workspace is emptied.
  - On a Lua config, a runtime window rule (`Hyprland::addWorkspaceRule`) maps windows first titled
    `omastrator-standin-…` straight onto `special:omastrator-spare`, silently. It is added before the first
    stand-in, and again after a `configreloaded` event, since a reload drops it. It's runtime state only:
    nothing is written to the config, and without it stand-ins map as before.
  - hyprctl prints a failed `eval` on stdout, with exit code 7, and we read only stderr, so a refused move
    looked like success. `runProgram` now takes stdout, or the exit code, as the reason.
- Seen once and not reproduced in six more runs: two fast switches left the middle page's workspace
  deleted, with a stand-in tiled beside the editor. Watch for it.

### Left for the author

By hand, on a real desktop:

1. Turn on View ▸ Pages as Workspaces with a two-page document and check
   that **Super+Tab** (Omarchy's `e+1` and the swipe) reaches the named
   workspaces, and that the editor swaps in. If `e+1` skips named
   workspaces (they get negative ids), correct BROWSER-FRAMES.md: pages are
   reached from the Pages list and Next/Previous Page.
2. Check the Lua move and focus dispatches (above) on a Lua config.
3. Watch for the flicker on New Page.
