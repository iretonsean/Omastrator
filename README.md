# Omastrator

Omastrator is a vector illustration app for Linux, built for
[Omarchy](https://omarchy.org). It works the way Illustrator does: the same
tools, the same shortcuts (Ctrl for ⌘) and the same menus. It follows your
Omarchy theme, hands AI work to the agent you already use, and reaches past its
own window through Omarchy's shell: an island under the bar, screen capture,
voice commands and live editing of your websites.

![Omastrator with a poster open, the Layers panel and the Properties panel](docs/screenshots/hero.png)

More in [Screenshots](#screenshots).


Omastrator's thesis is that it keeps Illustrator's depth, makes it feel like Figma and Paper, and puts AI at the core, so a designer spends their time on the customer, not the menus. More in [docs/VISION.md](docs/VISION.md).

## Features

- Selection and direct selection, with scale and rotate handles, marquee
  selection, and smart guides that snap to other objects and the artboard.
  Drags show Δx/Δy, W × H or the angle as you go.
- A contextual task bar under the selection with its likeliest next actions:
  fill and stroke, Edit Path and Path ▸ for a path; Pathfinder ▸, Shape
  Builder, Align ▸ and Group for several; font, size, Create Outlines and
  Area/Point Type for type; Image Trace and Vectorize with AI for an image;
  Ungroup and Isolate for a group. It ends with an **Ask AI…** field and ⋯ for
  the full right-click menu. It steps aside during drags, typing and AI
  previews, fades near a handle so clicks reach it, moves by its grip (right-click
  the grip to pin or reset it), and View ▸ Contextual Task Bar turns it off.
- **Ctrl+K** (or Ctrl+/, or Help ▸ Command Palette…) opens one box that finds
  every command, tool, panel, recent file, document setting and AI action by
  name, with its current (remapped) key. Recent commands come first. Type a
  request instead, or start with `?`, and the top row becomes "Ask Claude:
  …", which edits the selection or the document as a preview, or generates
  new art when nothing is selected.
- Right-click menus on the canvas and the Layers rows that list only what
  applies to what you clicked: Ask AI… first, a Select ▸ picker for stacked
  objects, and the rest one submenu away.
- A Select menu: Inverse, Next Object Above and Below, Same ▸ (fill, stroke,
  weight, opacity, blend mode, font), Object ▸ (text, images, clipping masks,
  open paths, stray points) and Reselect.
- Hold Alt to measure from the selection to whatever is under the pointer, or
  to the artboard's edges.
- Paste in Front, Back and in Place; Ctrl+D repeats the last transform, so an
  Alt-drag copy followed by Ctrl+D, Ctrl+D is step-and-repeat. Duplicate is
  Ctrl+Alt+D.
- Arrow keys nudge by a keyboard increment you set in Preferences or in
  Properties with nothing selected (Shift for ten times it, Alt to nudge a
  copy; with only type selected, Alt+arrows adjust the type instead). Number keys set opacity: 5 is 50 %, 0 is
  100 %, and two quick digits make an exact value.
- Zoom to Selection (Shift+2) and Fit Artboard (Shift+1).
- Pen, pencil, line, rectangle, rounded rectangle, ellipse, polygon and star
  tools, all producing editable Bézier paths.
- Type, edited in place on the canvas: click for point type, drag a box for
  area type that wraps and justifies, and convert between them. A Character
  section in Properties holds font, the font's real styles, size, leading,
  tracking and alignment, with kerning, baseline shift, scale, case, underline
  and strikethrough under Show more. Illustrator's keys work too: Alt+←/→
  tracking (with Ctrl, five times as much; at a caret, kerning), Alt+↑/↓
  leading, Alt+Shift+↑/↓ baseline shift and Ctrl+Shift+. / , size. Select
  characters while typing and every type field, a fill colour or a style
  changes just them, so one word can be bold, larger or red. OpenType features
  (ligatures, small caps, fractions, figure styles, stylistic sets) sit behind
  a button in Show more, with the ones the font lacks dimmed. Area type gets a
  Paragraph section for indents, a hanging first line, space before and after
  and where a justified last line sits. Character and paragraph styles live in
  the document: make one from the selection, apply it from the style button in
  Character's heading or Window ▸ Type Styles, and redefine it everywhere at
  once; a "+" shows local changes. Type ▸ Find/Replace Font… lists the fonts
  used, flags missing ones (also noted when the file opens) and replaces them
  in one step. Create Outlines turns type into paths, a path per run colour.
- A Properties panel in Figma's order, whose sections fold away and show only
  when they apply. Number fields take arithmetic and units (`2*(3+1)`,
  `25mm`, `50%`), `+10` or `*2` applied to each object, arrow steps, and a
  drag on the label to scrub. Transform has a 9-point reference point, a
  proportions link and Scale Strokes & Effects. Different values read Mixed.
- Fills and strokes: solid colours and linear or radial gradients, stroke
  weight, caps, corners and dashes, opacity and blend modes.
- Layers and groups with visibility, locking and drag-to-reorder, clipping masks
  and compound paths.
- Pathfinder (Unite, Minus Front, Intersect, Exclude), plus Outline Stroke,
  Offset Path and Simplify.
- Align and distribute, arrange, and transform: move, rotate, reflect and scale.
- Image Trace, which turns placed images into filled paths in black and white
  or in colour.
- Files:
  - saves and opens `.omai` documents (oh my)
  - imports SVG
  - exports SVG, PDF (kept as vectors), PNG and JPEG
  - places JPEG, PNG, TIFF, WebP and GIF images
  - opens and saves on cloud storage as well as on this computer
    ([Cloud storage](#cloud-storage))
  - shares the artboard or the selection as a link in one press
    ([Share with client](#share-with-client))
- Tabs for several documents at once, and every keyboard shortcut can be
  remapped.

## Cloud storage

Open, save, place and export on Google Drive, Dropbox, OneDrive, iCloud Drive,
Box, Proton Drive, pCloud, Mega, S3 (and R2, Wasabi, MinIO), Backblaze B2,
Nextcloud or WebDAV, SFTP, or anything else [rclone](https://rclone.org)
reaches.

- **Connect** with File ▸ Connect Cloud Storage… or Cloud Storage… on the
  welcome screen. Most services sign in through your browser. rclone keeps
  every sign-in, and Omastrator never sees or stores one. Without rclone, the
  sheet offers to install it (`omarchy pkg add rclone`).
- **Open, Save As, Place and Export** then list This Computer and each service
  you've connected, and remember where you were.
- **Save** writes to this computer first, then uploads in the background.
  Offline saves upload when the service is back.
- **If someone else changed the file**, nothing is overwritten. You choose
  Keep Both, Overwrite or Open Theirs.
- **Recent files** show which service each document lives on.

More in [docs/CLOUD-STORAGE.md](docs/CLOUD-STORAGE.md).

## Share with client

**Share**, at the top right of the window (also File ▸ Share, Ctrl+Alt+Shift+S,
Ctrl+K, and Share Selection in the right-click menu and the task bar's ⋯),
shares the artboard, or the selection if something is selected. The link goes
on the clipboard and a toast says "Link copied — Google Drive", with Open and
Copy Again. There's no dialog.

- **Where it goes:** a connected cloud service that makes links (Google Drive,
  Dropbox, OneDrive, Box, pCloud, S3, B2 and others), into
  `Omastrator Shares/<document>/` there. Otherwise GitHub through `gh`: SVG as
  a secret gist, PNG and PDF as an asset on a release in a public
  `omastrator-shares` repository on your account. GitHub asks once before the
  first upload, because neither of those is private. With neither connected,
  the toast says so and offers Connect Cloud Storage… and Connect GitHub.
- **Live:** with a project in Live, Share copies the latest deploy's URL if
  nothing has changed since, and otherwise runs a preview deploy (`vercel
  deploy`, a `netlify deploy` draft, Cloudflare's preview, or your own
  `"preview"` command). It never deploys to production.
- **Options:** the arrow beside Share picks the format (PNG at 2× by default,
  PDF or SVG) and the destination. Each document remembers them.
- **Shared Links…** lists what each document has shared, with Copy, Open and
  Unshare (which deletes the file, gist or release). **Paste Client
  Feedback…** takes the client's reply and runs Edit with Instruction on what
  was shared. The result is a preview you keep or discard.

The list lives in `~/.config/omastrator/shares.json`, never in the `.omai`.
More in [docs/SHARE.md](docs/SHARE.md).

## AI, with the agent you already use

Omastrator bundles no model. It hands work to your Omarchy default agent, such
as Claude Code, Codex, opencode or Gemini, and runs it in the background: no
terminal window and no permission prompts. The agent may only read what it's
given and call Omastrator, and the panel shows it working ("Claude is
roasting… 12 s") with Cancel. If it stops without an answer, the panel says so
and **Show log** opens what it printed. Anything the agent changes shows as a
preview: Enter keeps it as one undo step, and Esc throws it away.

- **Object ▸ Generate…**: describe what you want and choose how many
  variations. Pick one from the Variations panel, then refine it ("rounder",
  "fewer colours").
- **Object ▸ Edit with Instruction…**: for example "recolor to a sunset
  palette" or "make these icons consistent". The task bar's Ask AI… field and
  Ctrl+K reach it without a sheet.
- **Object ▸ Image Trace ▸ Vectorize with AI…**: a classic trace first, then
  the agent cleans it up. There are two modes, *Logo & icon* and *Sketch & line
  art*.
- **Roast My Design** (the flame at the bottom of the tool rail): a savage
  roast of the design, then sincere, specific fixes, then one click to make
  variations from that feedback.
- **Help ▸ Connect an Agent…**: drive Omastrator from any agent. Use
  `omastrator agent <method>` from a shell, or register the MCP server with
  `claude mcp add omastrator -- omastrator --mcp`. To watch the agent work,
  tick "Open the agent in a terminal while it works".

Choose your agent in Omarchy → Setup → Default → Agent. How it works:
[docs/AI-DESIGN.md](docs/AI-DESIGN.md). The rest of the app's personality is
described in [docs/HUMOR.md](docs/HUMOR.md).

## Across Omarchy

Omastrator also works outside its window, through Omarchy's own shell
([docs/OS-SUITE.md](docs/OS-SUITE.md)):

- **The island**: a pill under the bar with modes. *Draw* holds the canvas
  tools; *Capture* picks a colour anywhere on screen, traces a screenshot
  region, pastes clipboard SVG as paths, or loads your theme's colours as
  swatches. *AI* starts Generate…, Edit with Instruction…, Roast My Design and
  Vectorize with AI, and shows when your agent is working.
- **Live**: open a web page, or a project folder on this machine, in
  Omastrator's own Chromium. Click an element to select it (Shift-click adds),
  then change its text, colour, spacing, size, type or radius from a bar beside
  it. Values snap to the project's own tokens: its Tailwind theme, its CSS
  custom properties, then your Omarchy colours. Pages whose code isn't on this
  machine work as mock-ups. **Deploy** is the one button: it writes the
  changes into the code (the ones it can be sure of directly, the rest through
  your agent on a branch of its own), commits them, pushes, and deploys to
  production with the project's own setup: a remembered command, a `deploy`
  script, the Vercel, Netlify, Cloudflare or Fly CLI, a Makefile or
  `deploy.sh`, or else your agent. The deploy gets the project's `.env` files;
  their values never show anywhere. The first deploy of a project asks once.
  **Review changes** shows every write-back as a diff, with Discard, when you
  want it; **History** lists the commits (on GitHub through `gh`, which it
  offers to set up) with what was deployed, and restores any version.
- **Apps**: Live also opens Omarchy web apps as app windows, and Electron apps
  relaunched with their own profile. For GTK and Qt apps, **Capture Window**
  brings the focused app into Omastrator to redesign, and **File ▸ Hand to
  Agent…** gives your agent the mockup and the app's source folder; its change
  is written into the source, with its diff under Review changes.
- **Dictate**: hold the island's microphone (or Super+Alt+V) and speak. "Select
  the pen tool", "align left", "fill hash F F six six zero zero" run at once;
  anything else goes to your agent as an instruction. The island shows what it
  heard first, and Esc cancels. It uses Omarchy's voxtype
  (`omarchy voxtype install`), transcribing on your machine.
- **The tray light**: one glyph in the bar that shows when your agent is
  working, when results are ready, or when something went wrong. Click it for
  the island's AI mode.
- **Keys**: Super+Alt+D, C, A or L opens a mode, with Illustrator's tool letters
  inside Draw. Escape goes back.
- **Menu**: an Omastrator group in the Omarchy menu.

Set it up with:

```sh
omastrator setup            # shows each change as a diff and asks first
omastrator setup --apply    # also loads the keys from your Hyprland config
omastrator setup --remove   # takes out exactly what setup added
```

## Screenshots

Every picture is from demo mode: made-up documents, a sample website and an
agent that answers with prepared results. The island and tray light are the
shell plugins' own QML, rendered offscreen with Omarchy's Tokyo Night colours.

### Drawing

| | |
|---|---|
| ![Welcome sheet](docs/screenshots/welcome.png) | ![Smart guides](docs/screenshots/selection-smart-guides.png) |
| The welcome sheet: a new artboard, or a recent file. | Dragging the sun: transform handles and a smart guide on the artboard's centre. |
| ![Direct selection](docs/screenshots/direct-selection.png) | ![Pen](docs/screenshots/pen.png) |
| Direct Selection: a path's anchors, with the picked one's handles. | The Pen tool mid-path, with the next segment following the pointer. |
| ![Type on canvas](docs/screenshots/type-on-canvas.png) | ![Gradient fill](docs/screenshots/gradient-fill.png) |
| Point type, edited in place on the canvas. | A linear gradient fill, its end colour open in the picker. |
| ![Pathfinder before](docs/screenshots/pathfinder-before.png) | ![Pathfinder after](docs/screenshots/pathfinder-after.png) |
| Pathfinder, before: four shapes selected. | After Unite: one compound path, filled with a gradient. |
| ![Export](docs/screenshots/export.png) | ![Swatches](docs/screenshots/swatches.png) |
| Export PNG, with a preview, resolution and the file's size. | The Swatches panel, with the Omarchy theme's colours as a group. |
| ![Keyboard shortcuts](docs/screenshots/keyboard-shortcuts.png) | ![Connect an Agent](docs/screenshots/connect-an-agent.png) |
| Keyboard Shortcuts: every key can be remapped. | Help ▸ Connect an Agent: the socket, the MCP line and the CLI. |

### AI

| | |
|---|---|
| ![Generate sheet](docs/screenshots/ai-generate-sheet.png) | ![Variations](docs/screenshots/ai-variations.png) |
| Object ▸ Generate…: a brief and how many variations. | The Variations panel, with a refine field for the next round. |
| ![Proposal bar](docs/screenshots/ai-proposal-bar.png) | ![Edit with Instruction](docs/screenshots/ai-edit-sheet.png) |
| A picked variation is a proposal: Enter keeps it, Esc discards it. | Edit with Instruction, on the whole document. |
| ![Edit result](docs/screenshots/ai-edit-result.png) | ![Vectorize before](docs/screenshots/ai-vectorize-before.png) |
| The agent's recolour, waiting for Keep or Discard. | Vectorize with AI, before: a blurry placed JPEG of a logo. |
| ![Rough trace](docs/screenshots/ai-vectorize-trace.png) | ![Vectorize after](docs/screenshots/ai-vectorize-after.png) |
| The classic trace shows at once while the agent works. | After: the agent's redraw in 11 flat shapes. |
| ![Roast, page 1](docs/screenshots/ai-roast-1.png) | ![Roast, page 2](docs/screenshots/ai-roast-2.png) |
| Roast My Design at Spicy: the roast. | Page 2: three fixes, each with the value to use. |
| ![Roast, page 3](docs/screenshots/ai-roast-3.png) | ![Roast heat](docs/screenshots/ai-roast-heat.png) |
| Page 3: the brief, and Make variations from this feedback. | The Heat picker, from Friendly to Unhinged. |

### Across Omarchy

| | |
|---|---|
| ![Island modes](docs/screenshots/island-modes.png) | ![Island activity](docs/screenshots/island-activity.png) |
| The island resting and expanded: Normal, Draw, Capture, AI and Live. | Activity lines: agent work, results, Live and deploying. |
| ![Dictation](docs/screenshots/island-dictation.png) | ![Tray light](docs/screenshots/tray-light.png) |
| Dictation: listening, then what it heard and what it will do. | The tray light: idle, working, results ready, error. |
| ![Capture colour](docs/screenshots/capture-color-swatch.png) | ![Capture screenshot](docs/screenshots/capture-screenshot-trace.png) |
| Pick Colour as a Swatch: picked colours land in the Swatches panel. | Screenshot Region: a part of a web page, opened and traced in colour. |
| ![Live overlay](docs/screenshots/live-overlay.png) | ![Live spacing](docs/screenshots/live-spacing.png) |
| Live on a sample site: the contextual bar, the colour snapped to the site's `--accent` token. | Live's spacing handles: blue for padding, amber for margin. |
| ![Live review](docs/screenshots/live-review-window.png) | ![Setup](docs/screenshots/setup.png) |
| Review changes: a text change written directly, a colour change by the agent, each kept as a diff. | `omastrator setup` shows each change and asks first. |
| ![Menu entries](docs/screenshots/setup-menu-entries.png) | |
| The Omastrator group setup adds to the Omarchy menu. | |

## Build

You need C++20, Qt 6.4 or later (Widgets, Concurrent and Network), and CMake.
rclone is optional: cloud storage uses it, and one test runs it when present.

```sh
cmake -S . -B build
cmake --build build -j3
ctest --test-dir build
build/omastrator
```

If you have Docker, `scripts/dev.sh build|test|run` does the same in an
Ubuntu 24.04 container.

## Credits

Omastrator reuses code from other open-source projects:

- **[OmaPhoto](https://github.com/ZacharyZhang-NY/OmaPhoto)** by ZacharyZhang-NY,
  a Linux port of Wonder Assembly's
  [Compositor](https://github.com/robbietilton/Compositor). Most of the app
  shell comes from it: the theme, shortcuts, panels, tabs, layer list, colour
  picker and canvas navigation. Its Photoshop-style raster features were left
  out.
- **[omadesign](https://github.com/michaelmonetized/omadesign)** by Michael C
  Hurley: Image Trace and the smart guides are ported from its Rust code.
- **[nanosvg](https://github.com/memononen/nanosvg)** by Mikko Mononen: SVG
  parsing (zlib licence, see `third_party/nanosvg/LICENSE.txt`).

## Licence

MIT; see `LICENSE`.
