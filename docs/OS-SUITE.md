# Omastrator across Omarchy: spec and build plan

Written 2026-09-26 from a brainstorm with the user. Omastrator stops being only
a window and becomes a design layer for the whole desktop: a floating island
with modes, capture tools that work anywhere on screen, voice commands, and
live editing of websites and apps whose code is on this machine.

**Audience: any Omarchy user.** Nothing may assume one person's stack, host,
agent or config. The user's own setup (Claude Code, Vercel, a Graphite shell)
is one case among many.

## Principles

1. **One document owner.** The Omastrator app process still owns documents,
   undo and proposals. Everything new is a client that talks to it through the
   agent socket (`omastrator agent …`, see AI-DESIGN.md) or through new
   methods added there.
2. **Built on Omarchy's own surfaces.** The island and the tray light are
   `omarchy-shell` plugins, QML run by quickshell with the same
   `manifest.json` contract as `/usr/share/omarchy/shell/plugins` and
   `~/.config/omarchy/plugins/graphite.dock`. That gives layer-shell
   behaviour (always on top, never tiled, no stolen focus) and the theme for
   free. Setup follows Omarchy's pattern: one command that installs, and menu
   entries.
3. **Preview, then accept, where it's cheap; undo where it isn't.** Voice
   commands show what was heard before acting. Agent edits to a document are
   proposals. Live's code edits are written at once and recorded as a diff
   that Review changes shows and Discard takes out. Nothing deploys until the
   user presses Deploy.
4. **Opt-in for anything that widens access.** Browser remote debugging runs
   only in a dedicated profile, only for the session the user starts, and only
   on localhost. Setup never edits the user's Hyprland or shell config without
   showing the change and asking.
5. **Humor rules still apply** (HUMOR.md). The island is chrome: labels stay
   literal.

## Components

### 1. The island (`omastrator.island`, omarchy-shell plugin, kind `service`)

- A pill centred at the top of the focused monitor, under the bar. It follows
  the focused monitor.
- **Interim, pending a rethink of the island:** it shows only while an
  Omastrator window is focused, and stays while design mode is on, dictation is
  listening or a proposal or result waits. `omastrator island show always`
  (or Preferences) shows it everywhere. The tray light is always there. See
  ANYWHERE.md.
- **Three states:**
  - *resting*: small, shows the mode glyph
  - *expanded*: the mode's tools
  - *activity*: "Listening…", "Heard: …", "Working with Claude…", "3
    variations ready", "Saved · Preview deploying". It shows briefly, then
    returns to the previous state.
- **Modes**, switched by the arrow glyphs on the island or by keybinds. Each
  mode has a visible text label on first use and in tooltips.
  - **Normal:** the computer as usual. The island rests, and no keys are
    intercepted.
  - **Draw:** Figma-like tools that drive the Omastrator canvas: move, direct
    select, pen, pencil, rectangle, ellipse, polygon, star, shape builder, line, text,
    eyedropper, hand, zoom. They mirror `Tool` in EditorSession, and a new
    `select_tool` agent method keeps the island and the app in sync both
    ways. If Omastrator isn't running, choosing Draw starts it.
  - **Capture:** tools that act on the whole screen:
    - Pick Colour (hyprpicker) becomes the fill or stroke, or a new swatch
    - Screenshot Region (grim + slurp) opens in Omastrator and runs Image
      Trace; *Vectorize with AI* is offered next
    - Paste SVG pastes clipboard SVG as editable paths
    - Theme Swatches loads the current Omarchy theme's colours as a swatch
      group
  - **AI:** Generate…, Edit with Instruction…, Roast My Design, Vectorize with
    AI, Dictate. These are the existing flows in AI-DESIGN.md; the island only
    triggers them.
  - **Live:** live editing of web projects (component 5).
- **The contextual rule.** The island holds what is true about the session:
  the mode, the tools, agent status. Tools that depend on the current selection
  stay anchored to the canvas (in the Omastrator window) or to the element (in
  Live mode). The island never grows a second contextual toolbar.
- **Status stream.** The island reads `omastrator status --follow`, a new CLI
  command that prints one JSON line per change: app running, tool, mode,
  waiting task and agent, proposal title, variations ready, live session state.
  It reads it with quickshell's `Process` and a line parser, in the same style
  as `omarchy voxtype status`. Actions go through `omastrator agent <method>`
  or new `omastrator island …` subcommands.
- **Keybinds.** Setup writes `~/.config/omastrator/hyprland.conf` with a
  Hyprland submap per mode (suggested: Super+Alt+D Draw, Super+Alt+C Capture,
  Super+Alt+A AI, Super+Alt+L Live, Escape back to Normal). Setup prints the
  `source =` line and only appends it to the user's config with `--apply`.
  Tool letters (V, A, P…) work inside the Draw submap.

### 2. The tray light (`omastrator.ai`, omarchy-shell plugin, kind `bar-widget`)

- A small glyph for the bar's right section, with four states: idle, working,
  results ready, error.
- Clicking it opens the island in AI mode, and hovering shows the status text.
  It never opens a second AI menu.
- Setup offers to add it to `~/.config/omarchy/shell.json`: it shows the change
  and asks. Otherwise it prints the snippet.

### 3. Omarchy menu entries

User extension entries in `~/.config/omarchy/extensions/omarchy-menu.jsonc`,
under an "Omastrator" group: New Document, Island Mode ▸ …, Capture ▸ …,
Roast My Design, Connect an Agent. Setup merges them in and never overwrites
the user's other entries.

### 4. Dictation (the island's Dictate tool)

- **Engine: voxtype**, Omarchy's built-in dictation, installed by
  `omarchy voxtype install`. Omastrator does not install it. If it's missing,
  Dictate says so plainly, with the install command.
- **Capture the text; don't type it.** Voxtype normally types into the focused
  window. Find a supported way to capture a transcript instead: a one-shot or
  record/transcribe subcommand, the clipboard output mode with a private
  config, or `pw-record` into a WAV that voxtype transcribes. Use push-to-talk:
  the Dictate button or a keybind held while speaking.
- **Vocabulary.** Omastrator ships a design vocabulary: tool and command names,
  Pathfinder terms, "kerning", "tracking", units, colour names, hex spelling
  ("hash F F six six zero zero"). Pass it as the Whisper initial prompt or
  hot words if voxtype supports that. Otherwise normalise after transcription
  with a correction table (for example "path finder" → "Pathfinder", "minus
  front", number words → digits).
- **Two tiers:**
  - **Local grammar, instant, no AI.** Tool switches, align and distribute,
    arrange, group/ungroup, undo/redo, zoom, fill and stroke colour, stroke
    weight, opacity, "delete", "duplicate". Parsed into agent methods.
  - **Everything else** goes to Edit with Instruction as a proposal.
- The island shows "Heard: …" with the parsed action before running it. It runs
  after a short delay unless cancelled (Esc, or saying "cancel"), and
  immediately for tier-1 commands after the first use.

### 5. Live web editing ("Live" mode)

- **The browser session.** Starting Live mode launches Chromium in a dedicated
  profile (`~/.local/share/omastrator/browser`) with remote debugging on
  localhost at a random port, read from `DevToolsActivePort`. It never touches
  the user's normal profile. Omastrator speaks the Chrome DevTools Protocol
  over a small in-tree WebSocket client, so no new Qt module is needed:
  `qt6-websockets` isn't a dependency.
- **Projects.** A registry at `~/.config/omastrator/projects.json` maps an
  origin to a project folder. The first time, the user points Live at a folder.
  Omastrator suggests matches from git remotes, `.vercel/project.json`,
  `netlify.toml`, `wrangler.toml` and `localhost` ports, and asks to confirm.
- **Dev server.** Detect the project's dev command (package.json `dev`/`start`
  scripts via the lockfile's package manager, or a documented
  `omastrator.json` override). Start it and point the Live tab at it. Static
  sites with no script are served by Omastrator from the folder.
- **Selecting and editing, anchored to the page.** An injected overlay (via
  CDP `Runtime`/`Page.addScriptToEvaluateOnNewDocument`) does hover outline,
  click to select, and shift-click to add. A compact contextual bar sits next
  to the selected element:
  - text (edit in place)
  - colour and background
  - spacing (padding and margin handles on the element)
  - size
  - font size and weight
  - radius
  - "Ask AI…"

  Values snap to the project's own tokens: the Tailwind theme if present, then
  CSS custom properties found in the page's stylesheets, then the Omarchy
  theme's colours as the fallback palette. Changes apply live through CDP first.
- **Writing back:**
  - **Deterministic path**, used only when the edit is certain to be right:
    - Text: the old string occurs exactly once in the project's tracked
      source files (`git ls-files`).
    - A Tailwind class token swap: the class attribute string occurs once.
    - A CSS custom property value in one stylesheet.
    - Optional precise mapping when a framework plugin gives the source
      location (a small Vite plugin adding `data-oma-src`). Shipped as an
      optional helper, never required.
  - **Agent path, for everything else.** Launch the default agent with the
    project folder as its working directory. The prompt carries the selected
    element's HTML, its computed styles, a screenshot, the change requested and
    the URL. Work on a separate git branch or a stash-safe checkout, and merge
    the result around the user's uncommitted changes.
  - **The record.** Every write-back, deterministic or agent, keeps the bytes
    it changed. It is a background record, not a step: Review changes shows
    it as a diff when asked, with Discard. Nothing opens on its own.
- **Deploy is the primary action.** One Deploy (the island's Live row, the
  Live panel, `omastrator island live deploy`) writes back the pending edits,
  commits them, pushes, and deploys. There is no review gate in between.
  - *Uncommitted work stays the user's.* Omastrator commits only its own
    change to each file (merged with `git merge-file` around the user's edits
    in the same file); it stops only when the two truly clash.
  - *The deploy command* is the project's own: `omastrator.json`'s
    `"deploy": {"command", "cwd"}`, then package.json's `deploy` or
    `deploy:prod` script, then a host CLI whose config is there and which is
    installed (`vercel deploy --prod`, `netlify deploy --prod`,
    `wrangler deploy` or `wrangler pages deploy`, `fly deploy`), a Makefile
    `deploy` target or a `deploy.sh`. Otherwise **Deploy with agent**: the
    default agent deploys the pushed commit from the project's checkout with
    its existing setup, reports the live URL with `live_deployed`, and
    suggests the command it used, which the user can keep ("Remember this
    command" writes it to `omastrator.json`).
  - *The environment* is the project's `.env`, `.env.local`,
    `.env.production` and `.env.production.local`, loaded into the deploy's
    process (later files win; the files win over Omastrator's own environment
    except `PATH` and `HOME`). Values never appear in a prompt, log, status
    line or the UI; only key names do.
  - *Production is allowed.* The first deploy of a project asks once, "Deploy
    to production with `<command>`?", with "Don't ask again for this project".
  - *Progress* shows on the island's activity line and in the Live panel:
    Writing…, Committing…, Pushing…, Deploying…, then "Live at <url>" (the
    first https URL the deploy printed) or "Deploy failed: <one line>" with
    Details, the redacted log in `$XDG_STATE_HOME/omastrator/deploys/`.
  - *Save* is the same without the deploy: write back, commit, push.
- **GitHub for version history.** Through the `gh` CLI the user is logged
  into; "Connect GitHub" opens a terminal running `gh auth login`, and
  Omastrator never handles a token. A project with no remote is offered a
  private repository on its first save or deploy (`gh repo create <name>
  --private --source . --push`), confirmed once. Every save pushes.
- **History** (the Live panel, the island, `omastrator island live history`)
  lists the project's commits with their message, time, author, files, GitHub
  link and which were deployed where. Restore brings a version's files back
  as a new commit and offers Deploy; Discard after a deploy is a new commit
  that reverts those files, with Deploy offered again.
- **Guardrails.**
  - Live editing of any site works in the browser as a mock-up.
  - Write-back is only offered when the page maps to a registered local
    folder.

#### Live in your own browser

Live also joins a tab in the user's own Chromium, as it is, without
reloading it or opening a second browser.

- **The extension** (`extras/chromium-extension`, id
  `gmanolpmdkmgccoeiogpdhjifdkdjfap` from the key in its manifest) is loaded
  the way Omarchy loads its own: `omastrator setup` adds its folder to
  `--load-extension` in `~/.config/chromium-flags.conf`, and `--remove` takes
  it out. Its toolbar button opens a side panel, which is Live's start panel
  docked to the window. The panel shows the tab, its likely code folders (the
  Live sheet's list, from `live folders`) and Start. While Live runs, it shows
  the edits, Write back, Save, Deploy and Stop.
- **The way in** is a native messaging host. Setup writes
  `~/.config/chromium/NativeMessagingHosts/io.github.iretonsean.omastrator.json`,
  pointing at the omastrator binary. Chromium starts it with the extension's
  origin, which runs `omastrator browser-host`: a relay between Chromium's
  framed stdio and `$XDG_RUNTIME_DIR/omastrator-browser.sock`
  (`BrowserLink`). Its parent is Chromium's browser process, whose pid design
  mode uses to find the window, together with the tab's title.
- **DevTools through `chrome.debugger`.** The extension runs Omastrator's
  CDP commands on the joined tab, so the overlay, tokens, write-back,
  screenshots and the agent path are the same code as in Omastrator's own
  browser. The overlay is evaluated into the page already loaded, and
  registered for later navigations.
- **Ways out.** Chromium's "started debugging this browser" bar stays up
  while Live is in a tab, and its Cancel ends Live. So do Stop in the panel,
  `omastrator reset` and Super+Alt+Escape, closing the tab, and Chromium or
  Omastrator quitting. The extension takes the overlay out itself when
  Omastrator goes away. Leaving calls `__oma.leave()`, which disables the
  overlay's listeners and removes its layer, and then the debugger lets the
  tab go.
- **What's not the same:** the page is used at its own address. A
  registered production URL writes back to its folder, but doesn't switch to
  the dev server; open the dev server's localhost tab to see changes as they
  are written.

### 6. Native apps (last phase)

- **Electron, Tauri and Omarchy web apps:** relaunch with remote debugging in
  a dedicated profile, then the same Live pipeline.
- **GTK and Qt apps:** out of scope for write-back in this build. Offer
  *Capture to Omastrator* (screenshot → vectorize → redesign) and *Hand to
  agent* (mockup plus repo folder, for apps whose source the user has).
  Document what a later GTK Inspector or GammaRay integration would need.

### 7. Setup (`omastrator setup`)

- Installs the plugins by copying from the repo's `shell/` folder into
  `~/.config/omarchy/plugins/`.
- Writes the Hyprland conf, the menu entries and the default dictation
  vocabulary.
- Offers the bar widget, the Hyprland `source` line and voxtype.
- Every change to a user file is shown first and needs confirmation; `--yes`
  confirms all. `omastrator setup --remove` undoes exactly what setup added.
- Before it writes anything, setup copies every file it will change to
  `~/.local/state/omastrator/setup-backups/<yyyymmdd-hhmmss>/` (with a
  manifest of the original paths), keeps the newest 5, and changes nothing if
  the copy can't be made. `omastrator setup --restore [BACKUP]` shows what
  it would put back, asks (or `--yes`), copies what it is about to replace as
  a new backup, and restores; `--list-backups` lists them.
- Never takes a key you already use: it reads Hyprland's live binds and your
  config, skips that key and says so ("Super+Alt+C is already yours:
  skipped"). `--no-keys` installs with no global keys at all.
- Checks for the needed tools (hyprpicker, grim, slurp, wl-clipboard,
  chromium, voxtype) and names each missing one with the Omarchy or pacman
  command to add it.

## Build plan

Each phase ends green: the full ctest suite with `-DOMASTRATOR_WERROR=ON`, and
the phase's acceptance checks. Commit and push per phase to the `os-suite`
branch.

| Phase | Deliverable | Acceptance |
|---|---|---|
| 0 | `status --follow`, `select_tool`, mode state, `omastrator island …` CLI | Unit tests; the stream prints a line per change; the CLI round-trips against a test socket |
| 1 | Island plugin: states, Normal and Draw modes, tool sync | Loads in omarchy-shell without log errors; a tool chosen in the island changes the app, and the reverse; screenshots via grim |
| 2 | Capture mode: colour pick, screenshot → trace, paste SVG, theme swatches | Each action tested with fakes for hyprpicker/grim/slurp; SVG paste unit-tested |
| 3 | Tray light, menu entries, `omastrator setup` / `--remove` | setup/remove are idempotent in a temp HOME; diff shown before writing |
| 4 | AI mode in the island (triggers existing flows) | Flows start from the island; status shows waiting and ready |
| 5 | Live web editing: browser session, registry, dev server, overlay, contextual bar, token snapping | Headless Chromium tests against fixture sites (plain HTML, Vite + Tailwind): select, edit live, snap to tokens |
| 6 | Write-back and review: deterministic path, agent path, diff panel, Save, Publish | Fixture repos in temp dirs: text and class edits written exactly; Discard restores; Publish needs explicit confirmation and never runs in tests except to a local bare remote |
| 7 | Dictation: voxtype capture, vocabulary, grammar, Heard/confirm | Grammar and normalisation unit tests; a test path that transcribes a bundled WAV if voxtype is installed, skipped with a clear message if not |
| 8 | Native: Electron/Tauri via the Live pipeline; Capture to Omastrator for the rest | An Electron-style fixture (Chromium `--app`) goes through the Live pipeline headless |

## Testing rules for the live desktop

- The desktop is the user's real, running session.
- **Shell plugins:** install into `~/.config/omarchy/plugins/`, then
  `omarchy restart shell` and read its log. Find it with
  `quickshell list --all`, then `quickshell log -i <id>`. Plugins with
  `keepLoaded` don't hot-reload. Keep restarts few, and restore the user's
  shell config if a test changed it.
- **Input:** don't drive other windows with `wtype`. Screenshots with grim are
  fine.
- **Browser:** Chromium only in headless mode or in the dedicated profile;
  never the user's own profile.
- **Nothing outward-facing:** no real deploys and no pushes other than to this
  repo's `os-suite` branch.

## Decisions

Choices the spec left open, made while building it, in build order.

### Phase 0: status, tools and mode

- **The mode lives outside the app.** Normal and Capture must work with
  Omastrator closed, so the island's mode, its expanded flag and its activity
  line live in `$XDG_RUNTIME_DIR/omastrator/island.json` (gone at logout, so
  each session starts in Normal). `omastrator island …` writes it. The modes
  whose first-use label has shown are kept in
  `$XDG_STATE_HOME/omastrator/island-seen.json`. The app still owns documents,
  tools and proposals.
- **One stream.** `omastrator status --follow` merges the app's
  `status_follow` notifications with that file (watched with
  `QFileSystemWatcher`) and prints one compact JSON line when the merged
  object changes. Every key is always present, so QML never reads
  `undefined`: `running`, `document`, `tool`, `proposal`, `summary`,
  `waiting`, `task`, `agent`, `variations`, `variationsId`, `roastId`,
  `ready`, `error`, `live`, `mode`, `expanded`, `activity`, `activityId`,
  `activitySeconds`, `labelsSeen`. When the app closes, the app keys return
  to their defaults and the stream reconnects every half second.
- **Settled changes only.** The app publishes status 30 ms after the last
  change and only when it differs, per follower, so a drag or zoom does not
  flood the island.
- **`status_follow` is not an MCP tool.** It streams, which MCP tools don't;
  agents use `status_get`.
- **Starting the app.** `island mode draw` and `island tool …` start
  Omastrator when nothing answers on the socket and wait up to 15 s for it.
  `$OMASTRATOR_APP` replaces the command in tests.

### Phase 1: the island

- **Files.** `shell/omastrator.island/` is the plugin (kind `service`,
  `Island.qml`). Code both plugins share lives in `shell/omastrator-ui/`, a
  plain QML folder with no manifest that setup copies beside them, as
  `graphite-ui` is shared on this machine. It holds `Status.qml` (the stream),
  `Glyph.qml` and `Icons.js`.
- **Icons are drawn, not typed.** Every glyph is SVG path data on an 18-point
  square, drawn with `QtQuick.Shapes` in the theme's text colour. The tool
  icons copy the app's own toolbar (`src/UI/ToolIcons.cpp`), so the island
  and the canvas match and nothing depends on a Nerd Font codepoint.
- **Which binary.** The plugins run `omastrator` from `PATH`, unless
  `~/.config/omastrator/shell.json` names another `binary` (setup writes it
  when the running binary is not on `PATH`).
- **Surface.** One `PanelWindow` per screen, visible only on Hyprland's
  focused monitor. It is anchored to the top edge alone (so it is centred),
  on the `Top` layer, with `ExclusionMode.Normal` and a zero exclusive zone
  so it sits under the bar without reserving space. Keyboard focus is `None`
  and the input mask is the pill, so it never takes focus or blocks clicks
  beside it.
- **Activity is derived.** The island turns stream changes into its brief
  line: an explicit `island activity`, an error, "Working with Claude…" when
  a task starts, "3 variations ready", "Roast ready", and a waiting proposal.
  Each shows for its seconds, then the island returns to its state.
- **Clicks.** Clicking the mode glyph expands or rests; the arrows step
  modes. A click shows the chosen tool at once and the stream confirms it.
  First-use labels show beside the glyph for five seconds, then
  `island seen <mode>` retires them.

### Phase 2: Capture

- **Commands.** `omastrator island capture color [fill|stroke|swatch]`,
  `… screenshot`, `… paste-svg` and `… theme-swatches`. Each runs the outside
  program, starts the app if needed, calls a desktop method on the socket, and
  sets the island's activity line to the outcome (the plain error when it
  fails, with the `pacman` command for a missing program). Escape in
  hyprpicker or slurp is not an error.
- **User actions are not proposals.** Capture's methods (`apply_color`,
  `swatches_add`, `open_capture`, `paste_svg`) commit normal undo steps. They
  are marked `mcp: false`: MCP neither lists nor forwards them, so agents
  keep going through proposals. They refuse while a proposal or drag is open.
- **Pick Colour on the island:** click for fill, Shift-click for stroke,
  right-click for a new swatch. With nothing selected, the colour becomes the
  default for the next shape, as the app's own wells work.
- **Swatches.** The app had no Swatches panel, so Phase 2 adds one
  (Window ▸ Swatches): groups of named colours, kept per install in
  QSettings. Click sets the fill, Shift-click the stroke; right-click deletes.
  Picked colours go to the "Swatches" group. Theme Swatches reads
  `~/.local/state/omarchy/current/theme/colors.toml` (every `key = "#hex"`,
  in file order, named from the key) into "Omarchy: <theme>", replacing that
  group each time.
- **Screenshots** are kept in `$XDG_DATA_HOME/omastrator/captures/`, so the
  image outlives the trace. At startup the app deletes PNGs there older than
  30 days, always keeping the newest 20 (`Capture::pruneCaptures`; it touches
  only regular `.png` files directly in that folder). The capture opens as a new document the size of
  the region, places the image (one undo step) and traces it in colour (a
  second step), so Undo returns the raw screenshot. The traced group is then
  offered to Vectorize with AI: `status` carries `"offer": "vectorize"` while
  that group is in the front document. The AI mode (Phase 4) acts on it.
- **Paste SVG** reads `image/svg+xml` from `wl-paste`, else plain text that
  contains `<svg`. With no document open, it makes one the SVG's size.

### Phase 3: tray light, menu, setup

- **Tray light** (`shell/omastrator.ai`, kind `bar-widget`, default section
  right) builds on the shell's `BarWidget` and `BarIconButton`. Idle is the
  sparkle in the bar's text colour; working breathes in the accent colour;
  ready is the accent with a dot; error is the urgent colour. The tooltip is
  the plain status line; a click runs `omastrator island mode ai`.
- **Hyprland is Lua on Omarchy 4.** Omarchy 4's Hyprland 0.56 reads
  `~/.config/hypr/hyprland.lua`, so setup writes
  `~/.config/omastrator/hyprland.lua` there (with `hl.define_submap` and
  `hl.dsp.submap`), and `hyprland.conf` only where the user's config is still
  hyprlang. `--apply` appends one guarded line,
  `pcall(dofile, …/omastrator/hyprland.lua)`, or `source = …` for hyprlang.
  Both generated files pass `Hyprland --verify-config`, which the tests run
  when Hyprland is installed.
- **Keys.** Super+Alt+D, C, A and L were free in Omarchy's defaults. Draw's
  submap takes Illustrator's letters (V, A, P, N, M, L, \, I, H, Z); T chooses
  Type and hands the keyboard back, since typing needs the letters. Capture's
  keys (F, S, W, R, V, T) each run one action and hand the keyboard back.
  Escape returns to Normal. Clicking a mode on the island does not enter a
  submap, so no keys are ever taken by surprise.
- **shell.json is edited with jq** (an Omarchy dependency), which keeps the
  key order and layout the shell itself writes, so the diff shows only
  Omastrator's lines. The island is added to `plugins[]`; the tray light, if
  accepted, goes first in `bar.layout.right`. jq writes escaped characters
  raw (`\u2014` becomes "—"), so any line jq only re-encoded takes the
  file's own bytes back; found when a real `shell.json` held an escaped em
  dash. JSON edits are recomputed from
  the file as it is when applied, so declining one doesn't undo another.
  After a change, setup runs `omarchy-shell shell rescanPlugins` and
  `reloadConfig`.
- **Menu entries** go between `// BEGIN omastrator setup` and
  `// END omastrator setup` markers in the user's JSONC, the same pattern the
  user's own tools use. A comma is added after the user's last entry when it
  has none, and taken away again on removal.
- **Exactly what it added.** `~/.config/omastrator/setup.json` records the
  files and folders setup created, the shell.json entries, the menu block and
  comma, and the appended source line. `--remove` undoes only those, and a
  folder only once it is empty. The tests check a temporary HOME is
  byte-for-byte the same after setup and remove, and that each command run
  twice changes nothing the second time.
- **Backups and restore.** The backup is written before the first file is
  touched, and its manifest last, so a folder without one is a copy that never
  finished and setup hadn't started. `--restore` puts every file back to its
  copied bytes and deletes what setup created (and the folders it made, once
  empty), so it also works after an interrupted setup that never wrote
  `setup.json`. Files setup didn't change aren't in the backup and aren't
  touched. A restore copies the state it replaces first, as its own backup
  (action "restore"), so a second `--restore` undoes it; if that copy can't be
  made, nothing changes. The backup being restored from is never pruned by it.
- **Key clashes.** Only the global keys can clash: Super+Alt+D, C, A, L, V
  (dictation), the design and Desk keys and Super+Alt+Escape. The keys inside
  Omastrator's own submaps only exist while one is active. A key counts as
  taken when a live bind (`hyprctl binds -j`) or a bind in `~/.config/hypr`
  or Omarchy's defaults uses it outside a submap. **One rule says which binds
  are ours, and every check uses it** (`LiveBind` in `Setup+Keys.cpp`): a bind
  is ours when its own text says so (a description starting "Omastrator", or
  an argument naming omastrator), or when it is in the default submap on the
  same combo as one that does. Lua Hyprland reports every bind as `__lua` with
  a numeric arg, so a release bind or a hatch's second half has no text of
  its own, and key files from before this rule wrote some with none (the
  dictation release, `SUPER + ALT + V`). Binds inside our own submaps
  (`Escape`, `T`) never vouch for a default-submap bind, and a mouse bind is
  never ours by combo. That keeps a second run, and an upgrade from the
  previous key file, quiet. Without a live Hyprland, the config files are read: Lua
  `hl.bind`, Omarchy's `o.bind` and `o.bind_toggle` (never `unbind`), and
  hyprlang `bind*` lines, whose `$variables` are collected from every file
  first, since Hyprland shares them across `source`d files. Skipped keys are written to the
  key file's header and to `setup.json`. Binds by keycode (`code:24`) can't
  be compared with a key name and aren't detected.
- **A key file can't hurt the binds around it** (the tester's report, 2026-09-28:
  after setup the workspace keys, Super+number, and Super+Enter stopped
  working). The cause is not proven, because the failure hasn't been
  reproduced on a live desktop. Checked on Hyprland 0.56.2 with
  `Hyprland --verify-config` (read-only, no compositor started):
  - An error inside a `hl.define_submap` body doesn't leak. Hyprland catches
    it, reports "error in submap …", and the next bind lands in the default
    submap. So the theory that `pcall(dofile, …)` swallows an error and leaves
    a submap open is wrong for this version. The source line is at the end of
    the user's config, so nothing of the user's or Omarchy's loads inside our
    context; the hyprlang file starts with `submap = reset`, so an unclosed
    submap of the user's can't hold ours either.
  - `Alt_L`, `ALT + Alt_L` and `Escape` (and `escape` in hyprlang) are valid
    in both formats. Only an unknown key name (`Not_A_Real_Key`) fails, and
    only that one bind.
  - The likeliest cause is at run time: with an `omastrator-*` submap
    latched (Super+Alt+D, C, A, L, O, the island's own mode switch, dictation's
    heard prompt), every global bind is dead, Super+number and Super+Enter
    included. Nothing put the keyboard back if the island or the app stopped
    while a mode was on, and the Super+Alt+Escape hatch was itself a global
    bind, dead in exactly that state.
  What the key file does now: the reset key is `submap_universal` (hyprlang
  `binddu`, both of its binds described "Omastrator: reset"), so it works
  inside any submap, and it closes the submap itself before asking
  `omastrator reset`. Every bind, submap body and dispatch runs protected; a
  failure is a line in `~/.local/state/omastrator/setup.log` (trimmed once it
  passes 64 KB) and, once the file has finished loading, one Hyprland
  notification listing how many keys failed. The line that loads the file
  reports its own failure the same way, and setup recognises the older forms
  of that line and of the key file, so an upgrade rewrites them rather than
  adding a second copy. Leaving a mode closes the submap before it runs the
  island command, so a failing command can't leave a mode's keys held.
- **Reload check** (`Setup+Reload.cpp`). Nothing here ever says a key is back
  or working without looking. It runs when the keys or the source step are
  accepted, Hyprland loads our file (or will, once the source line is added)
  and Hyprland answers `hyprctl binds -j`:
  1. Before anything is written (the backup included), setup asks Hyprland's
     own program to check the user's config: `Hyprland --verify-config -c
     <their hyprland.lua or .conf>`, which starts no compositor. A reload loads
     the whole config again, so an edit of theirs that Hyprland hadn't loaded
     yet, and that is broken, would send Lua Hyprland to its emergency config
     (three keys) before setup wrote a thing. If the check reports errors,
     setup prints them, changes and reloads nothing, names `hyprctl
     configerrors` and exits 1 (`--no-keys` still installs without keys). When
     there is no `Hyprland` program to ask, setup says so and skips the whole
     reload check rather than guess; the keys load at the next reload or login.
     Then setup reloads (`reload config-only`, so monitors and runtime state
     stay) and reads the user's default-submap binds. This is the baseline:
     binds an autostart script added at run time (`hyprctl keyword bind`) are
     gone after any reload, so they are not ours to lose, and a valid pending
     edit of theirs is already in it, so it isn't blamed on us. If that reload
     fails, or Hyprland doesn't answer after it, there is no baseline: setup
     stops before writing anything, exits 1 and names `hyprctl configerrors`.
  2. After the writes it reloads again and asks again, for up to two seconds.
     If the reload fails, or Hyprland doesn't answer afterwards, that is
     reported and setup exits 1 with the files and the backup kept (never
     "Set up."). If keys of the user's are gone, setup restores every file
     from the backup, reloads, and **checks again**: it says "Your keys are
     back" only if they are; otherwise it says which are still missing with
     every file as it was, that Omastrator's files therefore aren't the cause,
     keeps the backup and exits 1. If all of the user's keys are there but some
     of Omastrator's own aren't bound, the files are kept and setup exits 1
     naming them.
  3. Setup doesn't reload when it isn't running under Hyprland, when
     `--no-keys` was given, or when nothing about the keys changed (a re-run
     with `--apply` that finds everything in place changes and reloads
     nothing).
- **Asking.** Each step is shown (plugin files by name, everything else as a
  unified diff) and asked about; `--yes` accepts all, `--dry-run` changes
  nothing, and a closed input answers no. Setup never installs packages: it
  names each missing program with its install command (voxtype's is
  `omarchy voxtype install`).
- **Where the plugins come from:** `$OMASTRATOR_SHELL_DIR`, else
  `<prefix>/share/omastrator/shell` (CMake installs `shell/` there), else the
  source tree the binary was built from. The vocabulary is written once and
  then left to the user.

### Phase 4: AI mode

- **One method.** The island starts flows with `omastrator island ai <flow>`,
  which calls the desktop method `ai_start`. The app runs the same code as its
  menus, so launching, waiting, proposals and panels behave exactly as in
  AI-DESIGN.md.
- **Typing happens in the app.** The island never takes keyboard focus, so
  Generate… and Edit with Instruction… bring Omastrator forward with their
  sheet open (the island says so). With `--prompt` they launch at once; that
  is how dictation (Phase 7) and scripts use them.
- **Vectorize with AI** takes the selected placed image, else the screenshot
  Capture traced last, whose group and original PNG go to the smart-trace
  prompt. The Capture row shows a Vectorize button only while that offer
  stands. Click is *Logo & icon*; Shift-click or right-click is
  *Sketch & line art*.
- **Stop** appears on the AI row only while the app waits for the agent, and
  stops waiting as the Variations panel's Cancel does.
- **Errors reach the island.** A flow that can't start (no agent chosen,
  nothing to roast) puts its plain reason on the island's activity line.
- **Keys.** The AI submap takes G, E, R and V, each handing the keyboard back.
  The menu gains Generate… and Roast My Design.

### Phase 5: Live web editing

- **Where it runs.** The Live session lives in the app process (the bridge
  owns one `LiveSession`), so its state reaches the island through the same
  status stream (`live: {state, url, project, mockup, edits, selection,
  message, server}`), and one process owns the browser, the dev server and the
  recorded edits. The island's Live row opens the Live sheet (a page, and which
  folder its code is in), toggles selecting, and stops; since Deploy-first Live
  it also holds Deploy, Review changes and History.
- **The browser.** Chromium (else Chrome) with
  `--user-data-dir=$XDG_DATA_HOME/omastrator/browser`,
  `--remote-debugging-port=0` and `--remote-debugging-address=127.0.0.1`; the
  port comes from `DevToolsActivePort`. It is a normal window for the user and
  headless in tests. Closing it ends Live.
- **DevTools client.** `WebSocketClient` implements RFC 6455's client side
  (masking, 16- and 64-bit lengths, fragments, ping and close) over
  `QTcpSocket`; `CdpConnection` matches answers to ids and uses flat sessions.
  Both are tested against a server written in the test from the RFC.
- **One place snaps.** The overlay never decides a value: it sends the
  property and raw value to the app, `TokenSet::resolve` snaps it, and the
  app calls `__oma.applyResolved()`. So the rules are C++ and unit-tested:
  - Tokens come from the page's CSS custom properties, resolved by the page
    (colours through a canvas, so `oklch()` works; lengths through a probe).
    Tailwind v4's theme variables (`--color-*`, `--spacing`, `--text-*`,
    `--font-weight-*`, `--radius-*`) are Tailwind tokens with their class
    suffix; `--spacing` gives the whole spacing scale. Other custom properties
    are CSS tokens. The Omarchy theme's colours come last.
  - Colours snap to the nearest token within a CIE76 distance of 18, Tailwind
    first, then CSS, then Omarchy; further away, the colour is kept. Lengths,
    sizes, weights and radii always snap when a scale exists.
  - When the element has a Tailwind class for that property (`p-4`,
    `bg-sky-500`, `text-3xl`; `text-` colour and size told apart), the class is
    swapped (`p-4` → `p-3`). If the page's CSS lacks the new class (Tailwind
    compiles only used classes), the value is also set inline until the dev
    server recompiles.
  - Tailwind v3 has no theme variables, so its pages snap to their CSS custom
    properties and Omarchy colours only, and class swaps aren't offered.
- **Dev servers.** `omastrator.json` (`{"dev": "…", "url": "…"}`) wins, then
  package.json's `dev` or `start` script run by the lockfile's package
  manager, then Omastrator's own static server for a folder with
  `index.html`. The URL is read from the server's output. It runs in its own
  process group with `BROWSER=none`, and stopping Live stops the group.
- **Registry and suggestions.** Starting Live with a page and a folder
  remembers the origin in `projects.json`. Suggestions come from localhost
  ports (the listening process's working folder, from `/proc`),
  `package.json` homepage, `.vercel/project.json`, `wrangler.toml`,
  `netlify.toml`, git remotes and folder names, one or two folders deep in the
  usual code folders.
- **Mock-ups.** A page with no folder is a mock-up: edits apply in the browser
  and are recorded, but write-back is not offered.
- **Fixtures without the network.** The Vite + Tailwind fixture commits the
  CSS Tailwind v4 compiles for it, and its `dev` script is a small Node server
  that prints Vite's banner, so the test exercises detection, `npm run dev`,
  URL discovery and snapping without installing packages.

### Phase 6: write-back and review

(Review as a gate, Keep, and Publish were replaced by Deploy-first Live,
below. The write-back paths still stand.)

- **Write Back is a step, not a side effect.** Edits apply to the page at
  once; the code changes when the user presses Write Back (the island's Live
  row, the review panel, or `island live writeback`). One press writes the
  certain edits and hands the rest to the agent.
- **Deterministic path** (`WriteBack::plan`), each only when certain:
  - Text: the old text occurs exactly once across the project's source files
    (`git ls-files --cached --others --exclude-standard`, so ignored files
    like `node_modules` never count). New text with markup characters
    (`< > & { }`) goes to the agent.
  - Tailwind classes: the element's original class attribute, quoted, occurs
    exactly once; every swap for that element is made in place in that one
    string, so class order in the source is kept.
  - A custom property (`--brand` on `:root`): declared exactly once across the
    stylesheets.
  - `data-oma-src="file:line:col"` from the optional Vite helper
    (`extras/vite-plugin-omastrator`, `vite dev` only) narrows the search to
    that line, so a repeated string can still be written. It is never
    required.
- **Agent path.** The agent runs in a git worktree on a new
  `omastrator/live-<time>` branch under
  `$XDG_DATA_HOME/omastrator/worktrees/`, with that worktree as its working
  directory and nothing written into it. The prompt carries the edits or the
  instruction, the selected elements (selector, classes, computed styles,
  markup), a screenshot of the selection and the URL, and asks it to finish
  with `live agentDone`. Its changes are then copied into the checkout as one
  review, and the worktree and branch are removed. Projects not in git get
  the deterministic path only.
- **Uncommitted work.** Before writing over a file with uncommitted changes,
  or before starting the agent on a project that has any, Live asks: the
  review panel offers Go Ahead Anyway. Omastrator's own pending writes don't
  count. A confirmed agent change is merged into the user's version with
  `git merge-file`; if they clash it isn't written at all.
- **Review.** Every write-back keeps the exact bytes of each file it touched
  (or that it didn't exist). The Live Review panel shows it as a diff with
  Keep and Discard; Discard restores those bytes, newest review first.
- **Save** commits only the files kept since the last save, with
  `git commit --only`, so anything else the user staged stays staged. The
  message is the change itself for one edit, or a list.
- **Publish** lists only what the project has: its git upstream
  (`git push <remote> HEAD:<branch>`, never forced), a Vercel preview
  (`vercel deploy`), a Netlify draft (`netlify deploy`) or a Cloudflare
  version upload (`wrangler versions upload`) where the project is set up for
  one and the CLI is installed. None deploys production. Each option states
  what it will run; it runs only when chosen, and only once everything is
  saved. The tests publish only to a local bare repository.

### Phase 7: dictation

- **Capture without typing.** Voxtype's daemon types into the focused window,
  so Omastrator doesn't use it. Push-to-talk records with `pw-record` (16 kHz
  mono, into the runtime folder, a minute at most), then runs
  `voxtype -q --initial-prompt <vocabulary> transcribe <wav>`, which reads the
  user's model settings and prints the text. Voxtype's config and service are
  never changed, and the recording is deleted once transcribed. Without
  voxtype, Dictate says so with `omarchy voxtype install`.
- **Vocabulary.** `--initial-prompt` is a global voxtype option (it goes before
  `transcribe`). The prompt is the vocabulary file (setup writes the default;
  the user can add words) with a spelled-hex example, cut to 800 characters,
  which is about Whisper's limit. With it, "hash F F six six zero zero" comes
  back spelled out rather than as "hat F6600".
- **Normalising** runs after: lowercase, punctuation out, colour/centre/grey
  one way, "path finder", "minus fronts", "eye dropper" and "pt" corrected,
  number words to digits ("twenty four", "one hundred five", "zero point
  five"; "six six" stays two numbers), and spoken hex after hash, hashtag,
  pound, hex or the common mishearing "hat" joined into `#rrggbb` when it
  spells 3, 6 or 8 digits. Anything else is left for the agent to read.
- **Tier 1** is a local grammar: tools ("select the pen tool", "use circle"),
  undo and redo, zoom, select all and deselect, group and ungroup, delete,
  duplicate, arrange, align (to the selection or the artboard), distribute,
  fill and stroke colours (hex or CSS names), stroke weight and opacity. They
  run through a new desktop method, `command`, as the user's own undo steps,
  not proposals.
- **Tier 2** is everything else: Edit with Instruction with the normalised
  words, as a proposal; in Live mode it is Live's "Ask AI…" instead.
- **Heard.** The island shows "Heard: “…” → what it will do". The first time a
  kind of tier-1 command is used, and for every tier-2 request, it waits
  2.5 seconds for Esc (through a Hyprland submap the keys file defines),
  a click on the island, or a spoken "cancel"; after that first use, the same
  kind runs at once. The kinds used are kept in
  `$XDG_STATE_HOME/omastrator/dictation-seen.json`.
- **Push-to-talk.** The island's Dictate button records while held. The keys
  file binds Super+Alt+V the same way, with a release bind; it passes
  `Hyprland --verify-config`.
- **Test recordings.** Two phrases, "Select the pen tool." (tier 1) and "Make
  the logo rounder." (tier 2), synthesised with Piper's LJ Speech voice
  (public-domain data) and resampled to 16 kHz mono. The test transcribes them
  with the installed voxtype and skips, saying why, when it is absent.

### Phase 8: native apps

- **Omarchy web apps** are Chromium `--app=` windows, so Live opens a page
  the same way (`island live start --url … --app`, or the Live sheet's "as an
  app window"): Omastrator's own profile, the same overlay and tokens.
- **Electron apps** are relaunched from their command line
  (`--command`, or the sheet's App field) with
  `--remote-debugging-port=0 --remote-debugging-address=127.0.0.1` and
  `--user-data-dir` set to a dedicated profile under
  `$XDG_DATA_HOME/omastrator/apps/<name>`. The separate profile means a
  second instance beside the user's own, with none of their signed-in state.
  The socket is read from the app's "DevTools listening on" line (or
  `DevToolsActivePort`). Omastrator never closes the user's running copy.
  A program that isn't Chromium-based is reported plainly. The page is
  reloaded once so the overlay runs; its URL decides write-back as for any
  page, and a project folder can be given explicitly (an Electron app's
  pages are usually `file://`, which has no origin to register).
- **Tauri isn't possible here.** On Linux, Tauri draws with WebKitGTK, which
  has no DevTools Protocol (its remote inspector speaks WebKit's own
  protocol, and only with `WEBKIT_INSPECTOR_SERVER` set before launch). Tauri
  apps get the GTK path below.
- **GTK and Qt apps: capture and hand off.**
  - *Capture to Omastrator* (`island capture window`, the Capture row, Capture
    mode's A key) screenshots the focused window using the geometry
    `hyprctl activewindow -j` gives, then opens and traces it like a region,
    with Vectorize with AI offered next.
  - *Hand to Agent* (File ▸ Hand to Agent…, the AI row, the menu) sends the
    document in front as a PNG and an SVG, with the app's source folder, to
    the default agent. It works as Live's agent path does: a git worktree on
    its own branch, `agentDone`, then its change is written and recorded
    (Review changes shows the diff), and Save or Deploy commits in that
    project. Reviews now carry their project, so Save commits
    each project's kept files there.
- **What a real GTK or Qt write-back would need** (not built):
  - GTK: GTK Inspector (`GTK_DEBUG=interactive`, GTK 3 and 4) can pick
    widgets and edit CSS live, but it has no external API. An integration
    would need a GTK module loaded with `GTK_MODULES` (GTK 3) or an
    `LD_PRELOAD` shim (GTK 4 dropped modules) that exposes the widget tree
    and applies CSS providers over a socket, and a map from widgets back to
    `.ui` or Blueprint files and code. GTK has no source locations for
    widgets, so the mapping would need the builder's object IDs.
  - Qt: GammaRay (KDAB, GPL) injects into a running Qt app and exposes
    QObjects, properties, QML and styles through its own client protocol.
    Omastrator would talk to GammaRay's probe (or ship a small probe of its
    own), select an item under the pointer, apply property changes live, and
    map QML items to their `.qml` file and line, which QML's debugging
    metadata provides. Widgets built in C++ have no such locations.
  - Either way, write-back would reuse Phase 6: deterministic when a location
    is exact, the agent otherwise, and every change reviewed as a diff.
- **Children die with the app.** Live's browser (and an Electron app it
  relaunched) and dev servers get `PR_SET_PDEATHSIG`, so a crash of
  Omastrator can't leave them running.

### Deploy-first Live

Decided 2026-09-27 with the user: "the diff should be a background review
that is revealed with a button and shouldn't be the focus", deploys use the
user's own setup and `.env`, and GitHub keeps the history.

- **One press.** Deploy (`AgentBridge::liveDeploy`) runs Writing… (the
  deterministic path, then the agent for the rest; it waits for the agent's
  `agentDone`), Committing…, then hands the slow part to `DeployJob`:
  Creating the GitHub repository… when one was chosen, Pushing…, Deploying….
  Save is the same pipeline without the deploy. Neither opens a panel.
- **Commits hold only Omastrator's change.** `WriteBack::commit` builds the
  commit in a separate index (`read-tree HEAD`, `hash-object`,
  `update-index --cacheinfo`, `write-tree`, `commit-tree`, `update-ref`). For
  each file the content is HEAD plus the difference between the file before
  Omastrator's first write since the last commit and the file now, through
  `git merge-file`; a clash stops the commit and names the file. The user's
  own uncommitted edits stay on disk, and what they staged stays staged.
  Commit hooks don't run, as with any plumbing commit.
- **The agent's work merges.** The agent's worktree change is merged with the
  file as it is when the agent finishes (the user's edits, or Omastrator's
  own), against the commit the agent started from. The old "Go Ahead Anyway"
  confirmation is gone: only a real clash stops it.
- **The record.** Every write-back is a `WriteBack::Review` with the exact
  bytes and, once saved, its commit. Review changes (the Live panel's button,
  the island, `island live changes`) shows the diffs newest first. Discard of
  an uncommitted one puts the files back (merged around later edits); of a
  committed one, it writes the reverse, records it ("Discard: …") and saves
  it as a new commit, then offers Deploy.
- **Resolving the command** (`Deploy::resolve`): `omastrator.json` `deploy`,
  package.json `deploy`/`deploy:prod` via the lockfile's package manager,
  `.vercel/project.json` or `vercel.json` with `vercel`, `netlify.toml` or
  `.netlify/state.json` with `netlify`, `wrangler.toml`/`.json`/`.jsonc` with
  `wrangler` (`pages deploy` when the config names
  `pages_build_output_dir`), `fly.toml` with `fly` or `flyctl`, a Makefile
  `deploy:` target with `make`, `deploy.sh`; otherwise the agent. Commands run
  with `/bin/sh -c` in the project folder (or the configured `cwd`), in their
  own process group so Cancel stops everything, stdin closed, at most 30
  minutes.
- **The environment.** `Deploy::parseEnv` follows dotenv: `KEY=value`,
  `export `, comments, single, double (with escapes, across lines) and
  backtick quotes, and inline ` #` comments on unquoted values. `.env`,
  `.env.local`, `.env.production`, `.env.production.local`, later winning, in
  the project folder and then the command's `cwd`. They override Omastrator's
  environment except `PATH` and `HOME`. Output is redacted line by line
  (every value of four characters or more becomes `[KEY]`) before it reaches
  the log, the status line or the failure message. The agent's prompt names
  the files and keys only. A command the agent suggests is offered to
  remember only when no value is in it.
- **The first deploy asks once.** The Deploy sheet says "Deploy to production
  with `<command>`?" (or "with Claude?"), lists the `.env` key names, and has
  "Don't ask again for this project". The answer is kept in
  `$XDG_CONFIG_HOME/omastrator/deploy.json` beside the registry, not in the
  project, so it never lands in a commit. The same sheet offers the GitHub
  repository; saying no is remembered too.
- **GitHub** is `gh` (`$OMASTRATOR_GH` in tests): `gh auth status` for the
  account (checked at most once a minute), `xdg-terminal-exec gh auth login`
  to connect (`$OMASTRATOR_TERMINAL` in tests), and `gh repo create <name>
  --private --source . --push`. Pushes go to the upstream, else `origin` (or
  the only remote) with `-u`, never forced, with `GIT_TERMINAL_PROMPT=0`. A
  project with no remote and no `gh` login still deploys; the panel says
  "GitHub isn't connected" with Connect GitHub.
- **Deploy records** are `$XDG_STATE_HOME/omastrator/deploys/index.json`
  (project, commit, URL, command, log, time, ok), beside one log per run.
  History marks a commit deployed from them. The live URL is the first
  `https://` URL the deploy command printed (trailing punctuation dropped),
  or what the agent reported.
- **The island.** The Live row is Deploy (with its label, on the accent
  colour), Review changes and History, plus Select and Stop while Live runs;
  they show whenever there's a project, Live running or not. Each stage is
  the activity line until the next; clicking it during a deploy puts the
  tools back, and clicking a failure opens Details. A failure reads "Deploy
  failed: <line>" and may carry one dry line after it, each at most once per
  install (`$XDG_STATE_HOME/omastrator/lines-seen.json`).
- **Removed:** Keep, Publish (and its preview-only rule), the Publish sheet,
  `WriteBack::publishOptions`, and the `live` actions `keep` and `publish`.
  New `live` actions: `deploy`, `cancel`, `history`, `restore`, `details`,
  `remember`, `github`; new method `live_deployed`.

### Share and preview deploys

Decided 2026-09-27 with Share with client (docs/SHARE.md). Share in a Live
project copies the latest deploy's URL when it's of the current commit and
nothing is unsaved; otherwise it runs a preview deploy, never production.
`Deploy::resolvePreview` finds it: `omastrator.json`'s `"preview": {"command",
"cwd"}`, package.json's `deploy:preview` or `preview:deploy`, `vercel deploy`,
a `netlify deploy` draft, `wrangler pages deploy --branch preview` or
`wrangler versions upload`. A Makefile, `deploy.sh` or Fly has no preview, and
the agent isn't asked to make one: Share says there's none. The run is a
`DeployJob` with `push` off and `preview` on; its record has `"preview": true`,
so History never marks a commit deployed from it.
