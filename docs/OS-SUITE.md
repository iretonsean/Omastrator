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
3. **Preview, then accept, everywhere.** Voice commands show what was heard
   before acting. Agent edits to a document are proposals. Code edits are a
   diff you keep or discard. Nothing deploys on its own.
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
    select, pen, pencil, rectangle, ellipse, polygon, star, line, text,
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
    the URL. Work on a separate git branch or a stash-safe checkout. Refuse to
    start if the files involved have uncommitted changes the user hasn't
    confirmed.
  - **Review.** Every write-back, deterministic or agent, is shown as a diff in
    a Live review panel with Keep and Discard. Discard restores exactly the
    files that changed.
- **Save and publish.**
  - *Save* commits the kept changes with a generated message.
  - *Publish* is a separate, explicit action. It offers only what the repo
    supports (git push to its upstream; the Vercel, Netlify or Cloudflare CLI
    if configured) and says what it will do first. It never force-pushes and
    never publishes to production on save.
- **Guardrails.**
  - Live editing of any site works in the browser as a mock-up.
  - Write-back is only offered when the page maps to a registered local
    folder.

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
  image outlives the trace. The capture opens as a new document the size of
  the region, places the image (one undo step) and traces it in colour (a
  second step), so Undo returns the raw screenshot. The traced group is then
  offered to Vectorize with AI: `status` carries `"offer": "vectorize"` while
  that group is in the front document. The AI mode (Phase 4) acts on it.
- **Paste SVG** reads `image/svg+xml` from `wl-paste`, else plain text that
  contains `<svg`. With no document open, it makes one the SVG's size.
