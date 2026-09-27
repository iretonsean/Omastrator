# Omastrator anywhere: every surface is a canvas

Decided with the author on 2026-09-27 in an interview, and read alongside
[VISION.md](VISION.md). This replaces the single-window idea. The contextual
toolbar exists everywhere in the OS, and every tool works on whatever you're
looking at.

## The idea

Omastrator is not an app you switch to. **There's no window by default.** It is
a design layer over the whole desktop. Point it at anything (a website, whether
yours or not, your terminal, a native app, the bar, the wallpaper) and every
tool is available on that surface.

## What you can do on any surface

All four are available everywhere, on every live site, including sites you
don't own.

1. **Draw on top.** A transparent canvas layer sits over the surface, so you
   can sketch, mock up, annotate and rearrange with every Omastrator tool.
2. **Measure and inspect.** Hover anything on screen for sizes, spacing,
   colours and fonts, with Alt-distance between elements, like devtools but
   OS-wide.
3. **Lift into vectors.** The surface's UI becomes editable shapes and text,
   in place: web pages from the DOM, other apps from the screen and the
   accessibility tree.
4. **Change the real thing**, where it's possible and yours to change:
   - **Your own sites:** Live, with Deploy.
   - **Other people's sites:** real DOM/CSS edits in Omastrator's browser.
     They never deploy, but can be kept, exported or handed to an agent.
   - **Web-based apps** (Electron, Chromium apps, Omarchy web apps): the same
     way as sites.
   - **Omarchy itself:** theme colours, fonts, bar layout, gaps, borders and
     wallpaper, changed visually on screen and written to the real config.
   - **GTK and Qt apps:** restyled through their theme and stylesheets, as far
     as the toolkit allows.
   - **Anything else:** mocked up on top, then Hand to Agent, with the app's
     source, to implement.

## How it feels

- **Click-through by default.** Overlays and the art on them are visible but
  don't steal clicks: you keep using the app underneath. You work on the art
  after selecting it, from the island or the floating bar (or its layer in the
  Desk).
- **The toolbar follows you. All three of these, together:**
  - **The island** holds the mode and the tools, and acts on the focused
    surface.
  - **A hotkey** enters design mode on the current surface; Esc leaves.
  - **A floating contextual bar** appears next to whatever you hover or
    select, anywhere on screen. It's the in-app task bar, now OS-wide.
- **AI in the bar.**
  - An **Ask** field is always there for the thing you're pointing at
    ("tighten this header", "pull this site's palette"). Results are previews
    you keep or discard.
  - **Proactive suggestions** show the likely next actions for this surface
    (extract tokens, measure spacing, mock up a variant).
  - **Onboarding** asks what kind of workflow the designer has, so the
    suggestions fit how they actually work. The answers can be changed later.

## Where work lives

- **The Desk** is one global, infinite canvas. It lives on its own Hyprland
  workspace (with a hotkey to jump there) and can also be opened as a normal
  window from the island or the launcher. Everything captured, drawn or lifted
  on any surface can land there as a frame labelled with its source (app, URL
  and time).
- **You choose each time** where a piece of work goes:
  - keep it as an overlay on that surface
  - send it to the Desk
  - send it to a document file
  - apply it to the source (where the surface allows)
  - hand it to the agent

  Omastrator remembers the choice as the default per surface.
- `.omai` documents and the file features (cloud storage, Share) keep working;
  a document is simply a frame or file you can open from the Desk.

## Design systems

A design system is **tokens** (colour, type scale, spacing, radii, shadows)
plus **components** with variants that update everywhere they're used. It can
live in four places, all supported:

1. **The project's code:** a `tokens.json` file, a Tailwind config, CSS
   custom properties. Omastrator reads and writes them.
2. **A global library** of personal tokens and components, available on every
   surface.
3. **Any live site:** point at a site, even one you don't own, and extract its
   colours, type scale, spacing and components into a system.
4. **Omarchy themes:** each theme is a design system. Edit it visually, create
   new ones, and apply them to the whole OS.

**Confirmation rule (the author's requirement).** Any push or pull of a design
system asks first, and shows exactly:

- where it will publish
- which files it will save, and where
- which repository, if any, it may commit to (and the branch)

Nothing is written, committed or published without that confirmation.

Built in phase 3: see [DESIGN-SYSTEMS.md](DESIGN-SYSTEMS.md) for what was
decided and how the confirmation dialog works.

## Build order

1. **Design mode everywhere (first).** The overlay layer with click-through,
   the hotkey, the OS-wide floating bar with Ask and suggestions, inspect and
   measure on any surface, drawing on top, and the Desk (its workspace plus the
   window). Onboarding for workflow-aware suggestions.
2. **Lift into vectors:** DOM to vectors for web; screen plus accessibility
   tree to vectors for other apps.
3. **Design systems:** tokens and components in the global library, extraction
   from any site, code sync, and Omarchy themes. All go through the
   confirmation rule.
4. **Change the real thing, widened:**
   - any site in Omastrator's browser, without deploy
   - visual Omarchy config
   - GTK/Qt styling
   - Hand to Agent for everything else

## Open questions

- ~~The exact design-mode hotkey and the Desk's workspace number~~: settled in
  phase 1 as Super+Alt+O and the special workspace `omastrator-desk` (see
  Decisions).
- How accessibility-tree lifting performs on large apps: capped at 800 nodes
  and four seconds in phase 2 (see Decisions); not yet measured on a real
  large app.
- ~~Privacy~~: onboarding says it plainly, and captures stay in Omastrator's
  own captures folder (see Decisions).

## Decisions

Choices the spec left open, made while building phase 1 (design mode
everywhere) on 2026-09-27.

### The background app

- **`omastrator --daemon`** starts the whole app without showing its window:
  the documents, the agent socket, the overlays and the Desk. Setup's
  Hyprland file starts it with Hyprland (`hl.on("hyprland.start", …)`, or
  `exec-once` in hyprlang). Anything that needs it and finds nothing on the
  socket (the island, `omastrator design`, `desk`) starts it the same way.
- **The window opens only when asked**: the Desk, a document sent from a
  surface, a panel or sheet (Variations, Roast, Live, Generate…), Draw mode,
  canvas work from the island (Capture, Paste SVG, voice commands), or a plain
  `omastrator [files]`. A second `omastrator` hands its files to the one
  running (`show_window`) and ends; `--daemon` with one running ends quietly.
  In the background, closing the window only hides it, with every document
  still open. `omastrator daemon stop` (the `quit_app` method) quits for real,
  asking about unsaved documents first.
- Started as before (`omastrator` with nothing running), the app shows its
  window and quits when it closes, so nothing changes for anyone who never
  uses design mode.

### Design mode and its keys

- **Design is the island's sixth mode.** The island's mode file is the one
  switch: the hotkey runs `omastrator design on`, which sets the mode, and the
  app follows the file. So the island, the hotkey, Esc and the arrows can't
  disagree. Leaving it from anywhere resets Hyprland's submap.
- **Super+Alt+O** enters design mode on the focused monitor and holds a small
  submap: **Escape** (or Super+Alt+O again) leaves, and **Alt**, held,
  measures (`Alt_L` pressed and released). Every other key reaches the apps;
  Hyprland's own shortcuts wait until Esc, as in the island's other modes.
  **Super+Alt+W** toggles the Desk. Neither is bound by Omarchy's defaults
  (checked against `/usr/share/omarchy/default/hypr/bindings`, and by a test).
  Both can be remapped with `"keys": {"design": "…", "desk": "…"}` in
  `~/.config/omastrator/anywhere.json`, then `omastrator setup`; anything that
  isn't a plain key combination is ignored. Both generated files pass
  `Hyprland --verify-config`.
- Design mode covers the monitor that had focus when it started. On the island
  its row holds Inspect, the drawing tools, Undo, the Desk, the questions
  (help) and Done.

### The overlay

- **Quickshell, inside the island plugin.** The overlay is `Overlay.qml` in
  `omastrator.island`: a `PanelWindow` per screen on the `Overlay` layer,
  exclusion `Ignore`, namespace `omastrator-overlay`. A Qt layer-shell window
  from the app would need LayerShellQt, which Omarchy doesn't install. The
  island already has the status stream, the theme and the focused monitor, and
  setup already installs the plugin, so nothing new has to be enabled.
- **Click-through by default.** The window's input mask is empty. The
  floating bar, Inspect's card, onboarding and the text box join it only while
  they're shown, and a drawing tool takes the whole monitor until Inspect gives
  it back. The rules are `OverlayLogic.maskMode`, tested in a JavaScript engine.
  The keyboard stays with the apps (`None`) unless something is being typed
  (the Ask field, onboarding, text and notes), when it is `OnDemand`.
- **Hover comes from the app, not from the overlay**, since a click-through
  surface gets no pointer events. The app reads Hyprland's cursor position
  from its socket every 100 ms while design mode is on. Moving, it shows the
  window (or page element) at once. Resting for two polls, it reads the
  accessibility tree and the colour under the pointer.
- **Art is drawn by the app and shown as pictures.** Each surface's layer is
  rendered to a PNG in the runtime folder, at the monitor's scale. It's drawn
  again only when the art changes, under a new name each time so the shell's
  image cache never shows an old one. The status stream carries each picture's
  place on screen, recomputed every 500 ms while art is on show. So shapes,
  arrowheads and type are drawn by Omastrator's own renderer.
- **The floating bar settles** 220 ms after what's under the pointer changes,
  and holds still while the pointer is on it, so reaching for it doesn't move
  it. Its actions name the inspection they were shown for, and the app keeps
  the last 48 by id, so the bar acts on what it showed even after the pointer
  has moved on.
- Art is selected from the bar (it offers the art's own actions and Deselect),
  from the island (Undo) or in the Desk, never by clicking through. A new
  drawing is selected, so the bar at once offers what to do with it.

### Inspect and measure

- **Pages** in Omastrator's browser (Live's, including sites that aren't
  yours, which open as mock-ups) are recognised by the browser's process id
  among Hyprland's windows, and read through the DevTools Protocol. The
  page's viewport is placed in its window by the window's size and
  `innerWidth`/`innerHeight` (the toolbars on top, even sides), and the same
  arithmetic runs in the page and in the app. Colours are normalised through
  a canvas, so `oklch()` reads right. Another browser's window is a window,
  with "Open in Omastrator's Browser" on the bar.
- **Other apps**: AT-SPI through a small Python helper (`python3` with
  `gi.repository.Atspi`). It reads window coordinates, offset by Hyprland's
  window position, since Wayland gives accessible objects no screen positions.
  The helper gets 1.5 s; a slow or missing tree falls back to the window's
  bounds. The colour under the pointer is one pixel from `grim`.
- **Distances** are Figma's: the gaps between two boxes apart, the four insets
  of one inside the other, and the near edges of two overlapping. Alt anchors
  on what's hovered (or pinned) and measures to the next thing; the bar's
  Measure holds the anchor without Alt.

### Drawing on top

- **One document for every overlay**, `$XDG_DATA_HOME/omastrator/overlays.omai`,
  with a layer per surface named by its key: `window:<class>` (a terminal's
  title changes too often to anchor to), `web:<address without the fragment>`
  or `desktop:<monitor>`. Art is kept in the surface's own coordinates: a
  window's corner, or a page's scrolled origin, so it moves with the window and
  scrolls with the page. It saves itself shortly after each change, never
  while an agent's preview is on show.
- **Every drawing is one undo step** named for the tool and the surface ("Draw
  Rectangle on foot"), including the layer made for a surface's first drawing.
  Drawing is refused while a preview is on the overlay, so a preview can't be
  kept by accident.
- **The tools**: pen (freehand, smoothed with the pencil's curve fit),
  rectangle, ellipse, line, arrow, text and note (a pale card with its words,
  grouped). Strokes take the Omarchy theme's accent.

### The floating bar and Ask

- **Actions per surface kind** (literal labels):
  - a page element: Inspect (with Copy CSS), Lift, Mock Up (the rectangle
    tool, pinned to it), Measure
  - a window: Capture to Desk, Lift, Measure
  - the desktop: Capture to Desk, Measure
  - another browser: Open in Omastrator's Browser
  - art: the in-app task bar's actions for that kind of selection (Unite and
    Group, Ungroup, Create Outlines, Image Trace, Release Clipping Mask), then
    Duplicate, Delete and Undo
- **Ask runs the agent headlessly on the overlay.** The prompt describes the
  surface and what's pointed at (its bounds in the overlay's coordinates, its
  colours and font), with a screenshot of it kept in the captures folder. The
  agent draws with the ordinary edit methods on the surface's layer, as a
  proposal: the bar shows Keep and Discard, and keeping it is one undo step.
  While it runs, the agent's methods act on the overlay rather than the front
  document. Nothing is sent to an agent unless the user asks.
- **Suggestions** are rules, ranked by the onboarding answers: what they make
  (web, rice, icons, marketing), where they come from, and how much AI they
  want (quiet: one chip and never an AI one; some: two; lots: three). An AI
  chip carries the prompt Ask runs.

### Where work goes

- The bar's Send to row offers the overlay, the Desk, a new document, the
  source and the agent. The choice is remembered per surface in
  `anywhere.json` and shown as the default next time.
- **The Desk** takes a frame: the surface's screenshot (a page's viewport, a
  window, a monitor) with its art in place over it, labelled "source · HH:mm".
  Frames run left to right in rows up to 6,000 points wide, and the artboard
  grows to hold them. Sending is one undo step, "Send to Desk".
- **A document** is a new, unsaved tab with the screenshot and the art at the
  surface's size.
- **The source** is offered only for a page open in Omastrator's browser with
  its code on this machine (Live's project). It goes through Live's agent path
  with a picture of the mock-up. Anything else says so plainly.
- **The agent** is Ask with the art selected.

### The Desk

- One document, `$XDG_DATA_HOME/omastrator/desk.omai`, opened as a tab called
  "Desk" when first needed and saved by itself half a second after each
  change. Opened behind a window already on show, it leaves that window's tab
  in front.
- **Its workspace** is the named special workspace `special:omastrator-desk`,
  so no workspace number can clash; `"deskWorkspace"` in `anywhere.json`
  changes it. `omastrator desk show` (Super+Alt+W, the island) shows the
  workspace, then maps the window again so it opens there. `desk window` (the
  launcher's "The Desk" action and the Omarchy menu) opens it where you are.
  On Omarchy 4's Lua config, dispatches go through `hyprctl eval`; on
  hyprlang, through `hyprctl dispatch`.

### Onboarding

- It opens by itself the first time design mode starts, until it's answered
  or skipped, and from the island's help button after that. There are three
  questions, each optional, and the answers are kept in `anywhere.json` under
  `onboarding`. It first says that captures stay on this machine and that
  nothing goes to an agent unless asked.

### Limits in phase 1

- Hyprland's own shortcuts pause while design mode holds Esc and Alt, as with
  the island's other submaps. Choosing Design on the island takes no keys, and
  Done leaves.
- AT-SPI coverage depends on the app: GTK and Qt apps expose their widgets,
  Flutter and Electron apps coarse panels, and terminals none. The window's
  bounds and the pixel colour always work.
- Only pages in Omastrator's browser are read as DOM; for the user's own
  browser, the bar offers to open the page there.
- Overlapping floating windows can show one window's art over another's.
- Art on a page is anchored to its address, and shows in whichever window
  Omastrator's browser has that page open.

## Decisions: Lift into vectors (phase 2)

Made while building phase 2 on 2026-09-27.

### What lifts, and how

- **Lift is on the bar** for a page element and for a window, and on the
  command line as `omastrator design lift [--target N] [--region X,Y,W,H]
  [--to overlay|desk|document]` (and `lift cancel`). A region on the desktop
  is traced.
- **Pages** (in Omastrator's browser, any site): one script
  (`src/Anywhere/LiftScript.h`) walks the DOM from the element the bar showed,
  found again by its box under its centre. Pointing at the page itself (`html`
  or `body`), or giving a region, lifts everything in the viewport (or the
  region), with the page's canvas colour as a "Page background" rectangle and
  one clip to the region. Per element:
  - its box: background colour and gradient layers as the fill stack
    (linear and radial; CSS's first layer on top), a live rectangle with the
    per-corner radii (a plain path when a corner is elliptical), and the
    border (one inside stroke when every side matches, dashed and dotted too;
    a filled ring when only the widths differ; a filled side each when the
    colours differ). Sharp box shadows become offset rectangles.
  - text: one text object per element with inline text. The browser's own line
    breaks are kept: each line is a paragraph, placed on the page's baseline
    (the word's box and the font's ascent share), with the gaps between lines
    as leading and each line's start as its indent. Real family (the first in
    the stack this machine has, as Chromium chose), size, weight, italic,
    letter-spacing as tracking, colour, underline, strike-through and
    uppercase. Inline children (`<b>`, `<span>`, links) become runs; their
    backgrounds become "Highlight" rectangles. Inputs show their value or
    placeholder.
  - images: the original file through the DevTools Protocol
    (`Page.getResourceContent`, so no cross-origin limits), drawn where
    `object-fit` and `object-position` put it and cropped to the box. An
    image that isn't in the page's cache is a screenshot of its box instead.
    An SVG file becomes vectors. Background images the same, placed once
    (not tiled).
  - inline SVG: cloned with its computed paint written onto each element and
    `<use>` copies resolved, then imported with the SVG importer at its size.
  - canvas, video and frames: a screenshot of the box.
  - structure: a group per element, named by `aria-label`, `#id` or
    `tag.class`; a wrapper that draws nothing and holds one thing is that
    thing. Children are in paint order (negative z-index, flow, floats,
    positioned, positive z-index). Opacity is the group's; a CSS transform is
    measured with the transform off and put back on the group, about its
    origin, so rotated and scaled things keep their true boxes. `overflow`
    other than visible makes a clip group from the padding box, rounded.
  - every object keeps **the element's selector** in `liftedFrom` (saved in
    `.omai`), for applying changes to the source later.
- **Other apps**: the whole accessibility tree through a second Python helper
  (up to 800 showing nodes, four seconds), over one screenshot of the window.
  Each node is a group named by role and name (its path of roles and indexes
  is its `liftedFrom`), a rectangle in the colour most of its pixels are where
  that differs from its parent's, its text as a text object with the tree's
  font (a flat crop of an icon or picture otherwise), all in the window's
  coordinates. A widget pointed at lifts that widget; a window, all of it.
- **No tree**: the screenshot is traced (eight colours, at most 1,400 pixels a
  side) and placed over the original. The bar then offers **Ask Agent to
  Clean Up**, which runs the agent headlessly on the traced group as a
  preview to keep or discard, like Ask.

### Where it lands

- The surface's remembered destination decides: the overlay (the default),
  the Desk (a frame labelled with the source, moved to the frame's corner) or
  a new document the size of the art. Source and agent aren't places for art,
  so they fall back to the overlay. `--to` chooses, and is remembered.
- Each is **one undo step, "Lift <thing>"**, in the session it lands in; on
  the overlay the surface's layer, if new, is made in the same step. The
  lifted group is selected, so the bar shows its actions.

### Performance

- Caps: 1,500 elements, 40,000 characters and 80 pictures on a page; 800
  nodes in a tree. A capped lift says what it left out.
- The lift runs in the background: the page's answer and each picture arrive
  as separate DevTools replies, and an app's tree, screenshot and trace are
  read on a worker thread. The bar shows the stage ("Reading the page",
  "Fetching pictures 3 of 8", "Building shapes", "Tracing") with **Cancel**;
  a cancelled lift lands nothing. One lift runs at a time.

### Limits in phase 2

- Blurred and inset box shadows, text shadows, filters, blend modes,
  `clip-path`, masks and pseudo-elements (`::before`, `::after`) aren't
  lifted. Tiled backgrounds are placed once. The individual `rotate`,
  `scale` and `translate` properties aren't read; `transform` is.
- Stacking is approximated within each parent; nested stacking contexts
  across parents aren't reordered.
- Text keeps the page's line breaks, so editing it doesn't reflow across
  lines; justified text is set flush left.
- AT-SPI extents and fonts are only as good as the toolkit's; Flutter and
  Electron apps give coarse panels, terminals none (so they're traced).

## Decisions: change the real thing, widened (phase 4)

Made while building phase 4's sites, Hand to Agent and lifted write-back on
2026-09-27. (Visual Omarchy config and GTK/Qt styling are decided separately.)

### Any site, without deploy

- **The same tools.** A page whose origin isn't registered as a project runs
  the whole Live overlay: select, the contextual bar (text, colour, spacing,
  size, type, radius), handles, and snapping to the page's own CSS custom
  properties (Tailwind v4 theme variables too, then the Omarchy colours).
  Every change is a real DOM or CSS change in Omastrator's browser.
- **Said plainly.** The page shows a small strip, bottom right: "Not your
  site: changes stay on this machine." The Live panel says the same in place
  of the project, the island's activity line adds it to the address, and the
  status stream's `live.site` carries it. Deploy and Save are hidden on the
  island and in the panel while such a page is open; `liveDeploy` refuses.
- **Edit sets.** Keep Edits saves the edits not kept yet as a named set for
  the origin (the name field, else "Edits 1", "Edits 2"…), in
  `$XDG_DATA_HOME/omastrator/edit-sets.json`, never in the site or the
  browser profile. A later change to the same element and property replaces
  the earlier one and keeps the page's first value. Each edit remembers its
  page's path, and comes back only there. Every enabled set is put back when
  the page loads (including reloads and new sessions); elements a framework
  renders late are caught for ten seconds. Sets are switched off and on in the
  strip's Edit Sets list or the panel's checkboxes, and removed with
  `removeEdits`. Edits not kept yet also survive a reload within the session.
- **Undoing on the page.** The overlay records each element's `style`,
  `class` and text before its first change, so switching a set off (and
  Before) puts the page back exactly as the site made it, then re-applies what
  stays on.
- **Export CSS…** writes what's on the page (or one set) as plain CSS or a
  userstyle, chosen by the file name (`.user.css`) or `format`. Rules are
  `!important`, grouped by selector and page; a value that snapped to one of
  the page's custom properties is written as `var(--name)`, so it follows the
  site. The userstyle has the `==UserStyle==` header and one `@-moz-document
  url-prefix()` per page. Text can't be CSS: text changes are listed in a
  comment. From the page, the app comes forward with a save dialog in
  Downloads.
- **Before and After to Desk** lifts the whole viewport with every edit off,
  then with them back on, and lands both as Desk frames ("host/path, before",
  "…, after") in one undo step, "Before and After to Desk". The edits go back
  on whatever happens to the lift.
- The `live` method's new actions: `editSets`, `keepEdits`, `toggleEdits`,
  `removeEdits`, `exportEdits`, `beforeAfter`, `original`, and `handoff` with
  `"page": true`. The strip's buttons go through the same code, queued so a
  dialog never opens inside a DevTools reply.

### Hand to Agent, from any surface

- **Where.** "Hand to Agent…" is on the floating bar for a page element, a
  window and art; on the page strip and the Live panel for a site that isn't
  yours; and `design handoff {surface|target, folder, prompt}`. File ▸ Hand to
  Agent… (the document in front) goes through the same path.
- **The package**, in one temporary folder the prompt names:
  - `mockup.png` (rendered at 2×) and `mockup.svg`: all the art on the
    surface, drawn and lifted, since the lifted UI says what the drawing is
    over. A page with no art hands over its screenshot as the mockup.
  - `selectors.json`: for each lifted element or widget, its `liftedFrom` (a
    CSS selector, or the accessible path of roles), its name and its box.
  - `edits.css` and a before → after list: a page's edits, kept and not.
  - the surface's screenshot now (grim for windows, DevTools for pages), and
    for a page with edits, a screenshot with them off.
- **The run** is the existing project path: a git worktree of the chosen
  folder on its own branch, headless where the agent allows, `agentDone`, and
  the change written and recorded under Review changes. Nothing opens by
  itself.
- **The folder** is asked for in the Hand to Agent sheet, which offers the
  one used last for that surface (`anywhere.json`'s `handoff`). Given in the
  call, it runs at once.

### Lifted vectors back to the source

- **Only your own page**: Apply to Source (the bar's Send to) for art on a
  page open in Live with its project. It first looks for lifted objects that
  changed; if there are none it falls back to the agent with a picture of the
  mock-up, as before.
- **What it compares with.** When a page's lift lands on the overlay, each
  lifted object's state is kept by id in `lifted.json` beside
  `overlays.omai`. Apply to Source compares the selected art (else all of it)
  with that, and afterwards records the new state, so nothing is applied
  twice.
- **What maps** (`LiftDiff`):
  - text object: its words (line breaks become spaces; only an element that
    holds text alone), fill → `color`, drawn size → `font-size`, face →
    `font-weight`.
  - an element's box: flat fill → `background-color`, live corner radii →
    `border-radius` (four values when they differ). A box with nothing in it
    resized → `width`/`height` by as much. A box whose edges moved around its
    content → `padding-*` by as much; content that only grew doesn't count.
  - Each becomes a Live edit (`LiveSession::edit`, snapped to tokens), then
    `liveWriteBack`: certain ones are written directly, the rest go to the
    agent, as with any Live edit.

### Limits in phase 4

- Edit sets match elements by the overlay's selector (an id, else a path of
  `nth-of-type`), so a site that reorders its markup can lose an edit; it's
  skipped quietly. Single-page apps that change the path without loading
  aren't followed until the next load.
- Userstyles can't change text; exported text changes are comments only.
- Apply to Source doesn't move things: a lifted element dragged elsewhere is
  said ("moves aren't applied"), and left for Hand to Agent. Gradients, images
  and borders on lifted art aren't mapped back. Apps' lifted widgets have no
  write-back of their own: Hand to Agent carries their accessible paths.
