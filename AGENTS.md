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
  JPEG) and `ImageImporter`. Errors are thrown as `FileError`.
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
  Heard), and `Vocabulary` is dictation's word list.
- `shell/` → the omarchy-shell plugins, QML: `omastrator.island` (the island),
  `omastrator.ai` (the tray light) and `omastrator-ui` (what they share).
  Setup copies them to `~/.config/omarchy/plugins/`. To try a change without
  touching the user's shell, run a throwaway `quickshell -p` config that loads
  the plugin, with `OMASTRATOR_SOCKET` and `OMASTRATOR_RUNTIME_DIR` pointed at a
  temporary folder.
- `src/Live` → `oma_live`. Live web editing (docs/OS-SUITE.md): an in-tree
  WebSocket client and the DevTools Protocol (`WebSocket`, `Cdp`), Chromium in
  Omastrator's own profile (`Browser`), dev servers and a static server,
  `ProjectRegistry`, `TokenSet` snapping, `LiveSession`, write-back
  (`WriteBack`, `AgentWork`), and Deploy (`Deploy`, `DeployJob`, `History`,
  with GitHub through `gh`). The page overlay
  is `overlay.js`, compiled in through `cmake/OverlayScript.h.in`. Headless
  tests run the fixtures in `tests/Live/fixtures` and skip without Chromium.
- `src/Canvas` → `oma_canvas`. `EditorCanvas` and its tools, `SmartGuides`,
  `Rulers` and `InlineTextEditor`.
- `src/UI`, `src/ContentView*` → `oma_ui`. The window, tabs, panels, menus,
  sheets, shortcuts and the Omarchy theme. Share with client
  (docs/SHARE.md) is `Share`, `ShareJob`, `ShareController` and
  `SharePanels`; its tests use the fake rclone and a fake `gh`.
- `src/OmastratorApp.cpp` holds `main`.
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
  wrapper types. A file over about 500 lines splits as `Name+Part.cpp`.
- **Humor:** follow `docs/HUMOR.md`. Menu items, buttons, data-loss prompts and
  accessibility text are never jokes.
- **AI features:** follow `docs/AI-ROADMAP.md`.
- **The user's desktop:** tests never touch the real shell, Hyprland or menu
  config. Setup tests run in a temporary `HOME`; outside programs are replaced
  through `OMASTRATOR_HYPRPICKER`, `OMASTRATOR_SLURP`, `OMASTRATOR_GRIM`,
  `OMASTRATOR_WL_PASTE`, `OMASTRATOR_OMARCHY`, `OMASTRATOR_OMARCHY_SHELL`,
  `OMASTRATOR_APP`, `OMASTRATOR_GH`, `OMASTRATOR_TERMINAL` and
  `OMASTRATOR_RCLONE`. Live's deploy tests push only to local bare
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
- **Dictation test recordings** (`tests/Agent/fixtures/dictation/*.wav`):
  synthesised with Piper's `en_US-ljspeech-medium` voice, trained on the
  public-domain LJ Speech dataset, then resampled to 16 kHz mono.
