# Omastrator: notes for agents

Omastrator is a vector illustration app for Linux, modelled on Adobe Illustrator
and made first for Omarchy. It is C++20 with Qt 6 Widgets (Qt 6.4 at minimum),
built with CMake and tested with Qt Test.

## Layout

Each folder builds as its own static library:

- `src/Document`, `src/Rendering` → `oma_core`. The model (`VectorDocument`,
  `VectorPath`, `Paint`), `EditorSession` (every edit, selection, history and
  view state; it emits `changed()` and `documentChanged()`), `DocumentHistory`,
  `DocumentCodec` (the JSON shared by `.omai` files and the clipboard),
  `PathOperations` (shapes, the boolean operations, offset, simplify),
  `ImageTrace`, and `VectorRenderer`, which the canvas and every export draw
  through.
- `src/IO` → `oma_io`. `ProjectStore` (`.omai`), `SvgImporter` (vendored
  nanosvg in `third_party/`), `SvgExporter`, `DocumentExporter` (PDF, PNG,
  JPEG), `ImageImporter` and `ScreenExport` (Export for Screens: artboards and
  export assets, a batch of scales and formats). Errors are thrown as
  `FileError`.
- `src/Cloud` → `oma_cloud`. Cloud storage through rclone
  (docs/CLOUD-STORAGE.md): `CloudStorage` runs it, `CloudLocation` is
  `remote:path` plus the cache, `CloudUploader` uploads in the background with
  conflict checks, and `CloudProviders` is the Connect list. Tests use the fake
  in `tests/Cloud/FakeRclone.cpp`; `CloudRcloneTests` runs the real rclone on a
  throwaway config and skips without it.
- `src/Agent` → `oma_agent`. The agent socket, CLI and MCP bridge
  (docs/AI-DESIGN.md), plus the desktop-wide commands in docs/OS-SUITE.md:
  `Cli` dispatches every GUI-less command, `Island` keeps the island's mode
  file and `omastrator island …`, `StatusStream` is `omastrator status
  --follow`, `Capture` runs hyprpicker, slurp, grim and wl-paste, `Setup` is
  `omastrator setup`, `Dictation` is push-to-talk (normalising, the grammar,
  Heard), `Vocabulary` is dictation's word list, `Hyprland` reads windows,
  monitors and the pointer from Hyprland's socket and runs dispatchers, and
  `DesignCli` is `omastrator design`, `desk` and `daemon`.
- `shell/` → the omarchy-shell plugins, QML: `omastrator.island` (the island),
  `omastrator.ai` (the tray light) and `omastrator-ui` (what they share).
  Setup copies them to `~/.config/omarchy/plugins/`. To try a change without
  touching the user's shell, run a throwaway `quickshell -p` config that loads
  the plugin, with `OMASTRATOR_SOCKET` and `OMASTRATOR_RUNTIME_DIR` pointed at a
  temporary folder.
- `src/Live` → `oma_live`. Live web editing (docs/OS-SUITE.md): an in-tree
  WebSocket client and the DevTools Protocol (`WebSocket`, `Cdp`), Chromium in
  Omastrator's own profile (`Browser`), dev servers and a static server,
  `ProjectRegistry`, `TokenSet` snapping, `LiveSession`, `EditSets` (edits to
  sites that aren't yours, kept per origin), write-back
  (`WriteBack`, `AgentWork`), and Deploy (`Deploy`, `DeployJob`, `History`,
  with GitHub through `gh`), and motion (docs/MOTION.md): `motion.js` (the page
  side, run after the overlay, compiled in like it), `Motion` (the timeline's
  rows from the page's list) and `MotionCode` (the marked blocks in a project's
  code). Browser View's Chromium (docs/BROWSER-VIEW.md) is
  `BrowserPool` (its own thread, tabs, idle stop, the cap) and `Breakpoints`
  (the widths a site's stylesheets name). The page overlay
  is `overlay.js`, compiled in through `cmake/OverlayScript.h.in`. Headless
  tests run the fixtures in `tests/Live/fixtures` and skip without Chromium.
- `src/Anywhere` → `oma_anywhere`. Design mode everywhere (docs/ANYWHERE.md):
  `DesignMode` (on and off with the island's Design mode, hover, Alt distances),
  `DesktopSource` (Hyprland, AT-SPI and grim; tests use
  `tests/Anywhere/FakeDesktop.h`), `Inspect` (the web inspector script, the
  AT-SPI helper, distances), `Overlays` (`overlays.omai`, a layer per surface),
  `Desk` (frames), `Bar` (actions and suggestions), `Lift` (a surface's UI as
  vectors: `LiftScript.h` walks the DOM, `Lift+Screen` reads the AT-SPI tree or
  traces, `LiftJob` runs it in the background, `LiftDiff` maps changed lifted
  page vectors back to page edits; tests fake the tree through
  `FakeDesktop::trees` or `OMASTRATOR_ATSPI_TREE`) and `AnywhereSettings`
  (onboarding, destinations, in `anywhere.json`). The app side is
  `src/UI/DesignController` behind the `design` method. The overlay itself is
  `shell/omastrator.island/Overlay.qml`; its decisions are in
  `OverlayLogic.js`, which `ShellPluginTests` runs in a `QJSEngine`.
- `src/System` → `oma_system`. Design systems (docs/DESIGN-SYSTEMS.md):
  `TokenFiles` (W3C tokens.json, Tailwind v4 and v3, CSS variables),
  `ProjectCode`, `Library` (the global library), `SiteExtract`,
  `OmarchyThemes` and `SyncPlan`; phase 4's `DesktopLook` (Omarchy's gaps,
  borders, bar, font, wallpaper and colours), `AppStyle` (GTK CSS, qt6ct and
  Qt stylesheets) and `ConfigBackup` (backups and Revert), with the app side in
  `UI/DesignController+Look.cpp` and `UI/DesktopLookPanel`; their tests build a
  fake Omarchy desktop in a temporary HOME (`tests/System/DesktopFixtures.h`).
  Every push or pull, and every write to the desktop's config, is a `SyncPlan` that only
  `UI/SyncConfirmDialog` can confirm; tests answer it with
  `SyncConfirmDialog::setResponder`. The model is `Document/DesignTokens`,
  `Document/Components` and `EditorSession+System.cpp`.
- `src/Canvas` → `oma_canvas`. `EditorCanvas` and its tools, `SmartGuides`,
  `Rulers` and `InlineTextEditor`.
- `src/UI`, `src/ContentView*` → `oma_ui`. The window, tabs, panels, menus,
  sheets, shortcuts and the Omarchy theme. Live inside a Browser View
  (docs/LIVE-IN-FRAME.md): `LiveFrames` (a frame's `LiveSession` on the pool's
  thread, snapshots, queued commands, held and pending edits), `ElementBar`
  (in `Canvas`) and `ElementBarActions`, and `BrowserViews+Site.cpp` (sites
  that aren't yours, This Is My Site), `BrowserViews+Deploy.cpp` (Deploy, Save,
  Review Changes, History) and `BrowserViews+Build.cpp` (Build It) on top of
  `AgentBridge`. The timeline under the canvas (docs/MOTION.md) is
  `MotionTimeline` (with `MotionTrackView`, the drawing) and `MotionInspector`.
  `DevServers` (in `src/Live`) starts a project's dev server
  for the window and the frames. Share with client
  (docs/SHARE.md) is `Share`, `ShareJob`, `ShareController` and
  `SharePanels`; its tests use the fake rclone and a fake `gh`. Send to a device
  (AirDrop) is `DeviceSend` and `ShareController+Device.cpp`, tested against a
  fake omdrop and omadrop.
  Pages as Workspaces (docs/WORKSPACES.md) is `PageWorkspaces` (+Place, +Sync)
  and `PageStandIn`; its tests run a fake Hyprland, `tests/UI/FakeHyprlandWorld.h`.
- `src/OmastratorApp.cpp` holds `main`. `omastrator --daemon` runs the app in the
  background with no window until one is asked for (`show_window`); a second
  `omastrator` hands its files to the running one.
- Tests live in `tests/<Folder>/*Tests.cpp`, one executable per file, found by a
  glob.

## Rules

- **The thesis:** read `docs/VISION.md` first. Keep Illustrator's power, reached
  the way Figma and Paper feel, with AI in the flow. Show only the essentials
  and put the rest one step away (disclosure, context menu, Ctrl+K). The
  designer stays the author.
- **Parallel builds:** keep them to `-j3` or fewer. A `-j10` build ran this
  15 GB machine out of memory.
- **Edits:** every document edit goes through `EditorSession` so it becomes one
  named undo step. Drags use `beginInteraction`, then a `preview*` call, then
  `commitInteraction` or `cancelInteraction`.
- **Style:** comments are one line and say why. Use Qt types directly and add no
  wrapper types. A file over about 500 lines splits as `Name+Part.cpp`. moc
  can't read a raw string with `)"` inside it (the test class silently gets no
  meta-object): keep such fixtures in a header, as `tests/Anywhere/InspectFixtures.h` does.
- **Humor:** follow `docs/HUMOR.md`. Menu items, buttons, data-loss prompts and
  accessibility text are never jokes.
- **AI features:** follow `docs/AI-ROADMAP.md`.
- **The user's desktop:** tests never touch the real shell, Hyprland or menu
  config. Setup tests run in a temporary `HOME`; outside programs are replaced
  through `OMASTRATOR_HYPRPICKER`, `OMASTRATOR_SLURP`, `OMASTRATOR_GRIM`,
  `OMASTRATOR_WL_PASTE`, `OMASTRATOR_OMARCHY`, `OMASTRATOR_OMARCHY_SHELL`,
  `OMASTRATOR_OMDROP`, `OMASTRATOR_OMADROP`, `OMASTRATOR_APP`, `OMASTRATOR_GH`, `OMASTRATOR_TERMINAL`, `OMASTRATOR_RCLONE`,
  `OMASTRATOR_HYPRCTL` (every Hyprland query and dispatch), `OMASTRATOR_HYPRLAND_EVENTS` (the event socket's path), `OMASTRATOR_HYPRLAND` (the `Hyprland --verify-config` check), `OMASTRATOR_ATSPI`,
  `OMASTRATOR_ATSPI_TREE`, `OMASTRATOR_WL_COPY` and `OMASTRATOR_GIT`. Design
  system tests use temporary projects, a temporary `HOME` for themes and
  libraries, and a fake Omarchy command. Unset `HYPRLAND_INSTANCE_SIGNATURE` in
  tests that
  build a `DesignController`, and give it a `FakeDesktop`. Live's deploy tests push only to local bare
  repositories and run fake deploy commands. Cloud tests never read the user's
  rclone config.
- **Commits:** public repo. Commit as the GitHub no-reply address, and never add
  personal data.

## Provenance

- **OmaPhoto** (ZacharyZhang-NY/OmaPhoto, MIT, itself a port of Wonder
  Assembly's Compositor): the theme, shortcuts, floating panels, viewport,
  history, workspace and tabs, layer list, colour picker, sheets, blend modes,
  canvas navigation, inline text editing and the packaging.
- **omadesign** (michaelmonetized/omadesign, MIT): `ImageTrace` and
  `SmartGuides`, ported from Rust.
- **nanosvg** (memononen/nanosvg, zlib): SVG parsing, with one patch marked
  "OmaIllustrator patch".
- **hyph-utf8's hyph-en-us** (hyphenation/tex-hyphen, `third_party/hyph-utf8`;
  Copyright © 1990, 2004, 2005 Gerard D.C. Kuiken, itself Frank Liang's
  patterns and the TeX hyphenation exception log; licence in
  `third_party/hyph-utf8/LICENSE`, permitting copying and modification with
  the notice kept): automatic hyphenation's patterns and exceptions, compiled
  in by `cmake/HyphenPatterns.h.in` and read by `Hyphenator.cpp`.
- **Dictation test recordings** (`tests/Agent/fixtures/dictation/*.wav`):
  synthesised with Piper's `en_US-ljspeech-medium` voice, trained on the
  public-domain LJ Speech dataset, then resampled to 16 kHz mono.
