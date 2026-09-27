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

- The exact design-mode hotkey and the Desk's workspace number, which must not
  clash with Omarchy's defaults.
- How accessibility-tree lifting performs on large apps.
- Privacy: captures of other apps stay on this machine, and nothing is sent to
  an agent unless the user asks. Onboarding should say this plainly.
