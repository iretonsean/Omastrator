# Design systems

Phase 3 of [ANYWHERE.md](ANYWHERE.md), built 2026-09-27. A design system is
**tokens** (colour, type, spacing, radius, shadow) plus **components** with
variants. It lives in the document, and it syncs with four places: the
project's code, a global library, any site, and Omarchy themes. Every push and
pull follows the author's confirmation rule.

## Where to find it

- **Window ▸ Design System** opens the panel with three tabs: Tokens,
  Components and Sources.
- **Object ▸ Components:** Make Component (Ctrl+Alt+K, Figma's key), Detach
  Instance (Ctrl+Alt+B), Reset Overrides and Select Main Component.
- **Ctrl+K** lists "Apply Token: …" for every token, "Place Component: …" for
  every component, and "Swap Variant: size = lg" for the selected instance.
- **Properties** shows a Component section for an instance (a menu per
  variant property, Reset and Detach) or a component (its instance count).
- **The in-app task bar** offers Detach on an instance.
- **The floating bar** (design mode) offers Extract Design System on a page in
  Omastrator's browser, and Make Component and Design System on art.

## Tokens

- Kinds: colour, type (family, weight, size, line height, tracking), spacing,
  radius and shadow. Names are slash groups (`color/brand/500`, `text/body`).
- **References.** A paint's `token` (fill and stroke, and every entry of the
  appearance stack), and `tokenRefs` on objects: `strokeWidth` (a spacing or
  radius token), `radius` (a live rectangle's four corners), `type` (a text
  object) and `gapX`/`gapY` (a group whose children are spaced by a spacing
  token, left to right or top to bottom). A text style follows a type token
  through `typeToken`, and restyles every text that uses it.
- This generalises global swatches: a swatch recolours paints that carry its
  id; a token does the same for colour, and the same mechanism for the other
  kinds. Swatches stay as they were (they belong to the install); tokens
  belong to the document and the library.
- **Changing a token** is one undo step ("Edit Token") that updates every use.
  A value changed by hand leaves the token (the reference is dropped, the look
  kept), so nothing snaps back unexpectedly. Deleting a token keeps every
  look.
- **Modes.** Tokens > + > Add Mode… copies each token's value into the new mode
  (the first mode is the token's own value). The mode menu at the top of the
  Tokens tab switches every use at once, as one step ("Switch to Dark Mode").
- Shadow tokens are kept, edited and synced to code, but objects have no
  shadow effect yet, so they can't be applied to art.

## Components

These also cover Illustrator's symbols (QOL P2-6).

- **Make Component** turns the selection into a main component: a group on the
  canvas (grouped first if it's more than one group).
- **Instances** are groups whose children are copies of the component's,
  placed by the instance's own transform. Moving, scaling or rotating an
  instance moves its placement; moving the component doesn't move its
  instances. Duplicating a component makes an instance of it.
- **Overrides.** Changing a layer inside an instance (its fill, its stroke
  colour, its text or its visibility) is recorded as an override for that
  layer, keyed by its name path (`Label`, `Icon/Path#2`). Editing the
  component then updates every instance in the same undo step, and the
  overrides stay on top. Other changes inside an instance (moving or reshaping
  a layer) are put back; detach it to edit its shape.
- **Variants.** Components sharing a set name are its variants, told apart by
  properties (`size = sm | md | lg`, `state = default | hover`). Add Variant
  (the component's right-click menu) copies the component beside it with one
  property changed. Swap an instance's variant from the Components tab, Ctrl+K
  or the task bar; overrides carry over by layer name.
- **Detach** keeps the look and stops following. Deleting a component detaches
  its instances.
- Instances inside a component are copied as plain groups into that
  component's instances (one level of nesting is kept live).

## Where a system lives

| Source | Pull (into the document) | Push |
|---|---|---|
| The project's code | tokens.json (W3C), Tailwind v4 `@theme`, Tailwind v3 config (read only), CSS custom properties | tokens.json, the Tailwind CSS and the CSS files found, or a new `tokens.json`; committed when the folder is a git repository |
| The global library | its tokens | tokens merged by name, components by whole set, in `$XDG_DATA_HOME/omastrator/libraries/<name>.json` |
| Any site | colours, type scale, spacing, radii, shadows and repeated components, as a proposal | (never) |
| Omarchy themes | the current theme's `colors.toml` and `shell.spacing.toml` | a theme folder in `~/.config/omarchy/themes/<name>`, then `omarchy theme set <name>` if asked |

### The project's code

- The Sources tab keeps a project folder. It finds `tokens.json`,
  `design-tokens.json`, `tokens/tokens.json`, `src/tokens.json`,
  `tailwind.config.{js,cjs,mjs,ts}`, and CSS files (up to four folders deep,
  skipping `node_modules`, `dist`, `build` and the like) with `@theme` or
  `:root` variables.
- **W3C design tokens:** groups, `$type` inherited from groups, aliases
  (`{color.brand}`), string and 2025 object forms of colours and dimensions,
  typography and shadow composites. Unknown types are listed as skipped. Modes
  are kept in `$extensions["io.github.iretonsean.omastrator"].modes`. Writing
  merges into the file and keeps every key it doesn't know; a new file uses
  the 2025 object forms, a file that uses strings keeps strings.
- **Tailwind v4:** `--color-*`, `--spacing` and `--spacing-*`, `--radius-*`,
  `--shadow-*`, `--text-*` with `--line-height`, `--letter-spacing` and
  `--font-weight`, and `--font-sans` for the family. `oklch()` and `oklab()`
  are converted. Values are changed in place; new ones go at the end of the
  `@theme` block (made after `@import "tailwindcss"` if there is none). A rem
  value stays in rem.
- **Tailwind v3:** `theme` and `theme.extend` are read as far as they are
  literals (`require()`d palettes and functions are skipped). It's never
  written; push writes the other files.
- **CSS custom properties:** `:root` for the base, and `.dark`,
  `[data-theme="dark"]` or `@media (prefers-color-scheme: dark)` for a dark
  mode. A variable keeps its own name (`--brand-blue` reads as
  `color/brand-blue` and is written back as `--brand-blue`).
- Name mapping: `color/brand/500` ↔ `--color-brand-500`; a first group that
  repeats the kind (`color`, `space`, `radius`, `shadow`, `text`) is the
  prefix.
- Push never deletes a token from a file.

### The global library

`$XDG_DATA_HOME/omastrator/libraries/<name>.json` (normally
`~/.local/share/omastrator/libraries`). Save to Library merges the document's
tokens by name and replaces each of its component sets. A library component
double-clicked in the Sources tab (or placed from the panel on the overlay)
goes in once, with its tokens, on a Components layer beside the artboard, and
an instance lands in the middle of the artboard. On any surface: the floating
bar's Design System opens the panel on the overlay's art.

### Any site

Extract (the Sources tab, with an address) opens the page in a headless
Chromium with a throwaway profile; the floating bar's Extract Design System
reads the page already open in Omastrator's browser. The page script counts
computed colours, fonts, spacing, radii and shadows, and finds elements with
the same tag and classes repeated three times or more. The proposal names the
commonest background and text colours, the most saturated other colour as the
accent, a type scale around the commonest size (`text/base`, `sm`, `lg`,
`xl`…), and each repeated element as a component (its box with its first line
of text, bound to the matching colour tokens). Nothing is sent to the site.

### Omarchy themes

- **Use Theme Tokens** reads the current theme
  (`~/.local/state/omarchy/current/theme`): every colour in `colors.toml` as
  `color/<key>` (Hyprland's `rgba(0a84ffb3)` form included) and the numbers
  in `shell.spacing.toml` as `spacing/<key>`.
- **Save Theme** writes a new folder in `~/.config/omarchy/themes/<name>`: the
  source theme's files copied as they are, with `colors.toml` and
  `shell.spacing.toml` rewritten in place (comments, order and the other keys
  kept). A theme of that name the user already has is changed in place; a
  folder that exists otherwise is refused. **Apply to the desktop** then runs
  `omarchy theme set <name>` ($OMASTRATOR_OMARCHY in tests).

## The confirmation rule

Every push and pull builds a plan first (`src/System/SyncPlan.h`), and
`SyncConfirmDialog` shows it before anything happens:

- **Publishes to / Brings into:** where, in plain words, with the full path
  (the project folder and its repository, the library file, the theme folder
  and whether the desktop switches, or "This document… No files are written").
- **Writes these files:** every file's full path, with "+3 −1 lines", "new, 12
  lines" or "copied from …".
- **Commits to:** the git repository's top folder, the branch checked out, and
  the message. It commits only the files it wrote, with the repository's own
  identity, and never pushes to a remote.
- **Then runs:** the command, in full (`omarchy theme set hot-pink`).
- **In Omastrator:** what changes in the document (always one undo step).
- **Preview:** the dry run: a summary ("2 files: 1 changed, 1 new") and each
  file's changed lines.

Cancel is the default button. Nothing is written, committed, run or applied
before Confirm: `SyncRunner::execute` needs a `Confirmation`, which only the
dialog can make. If a file changed between the preview and the confirmation,
nothing is written. Placing a library component into a document reads only,
so it doesn't ask.

## Files

- `src/Document/DesignTokens.*`, `Components.*`, `EditorSession+System.cpp`:
  the model and every edit. `.omai` version 4 adds `tokens`, `tokenModes`,
  `tokenMode`, paints' `token`, objects' `tokens`, `component` and
  `instance`, and text styles' `typeToken`. Version 3 files open with none.
- `src/System` (`oma_system`): `TokenFiles` (W3C, Tailwind, CSS),
  `ProjectCode`, `Library`, `SiteExtract`, `OmarchyThemes` and `SyncPlan`.
- `src/UI/DesignSystemPanel*`, `SyncConfirmDialog`.
- Tests: `tests/Document/DesignSystemTests.cpp`,
  `tests/System/DesignSourcesTests.cpp` (temporary projects and HOME, and a
  headless-Chromium fixture), `tests/UI/DesignSystemUiTests.cpp` (the dialog,
  git on temporary repositories, the fake Omarchy command).

## Limits

- Shadow tokens can't be applied to objects (no shadow effect yet).
- Instances keep their component's geometry: only fill, stroke colour, text
  and visibility override. Nested instances are one level deep.
- Tailwind v3 configs are read only, and only their literal parts.
- Type tokens write the family to CSS (`--text-x--font-family`) and W3C, not to
  Tailwind's `--text-*`; Tailwind's family comes from `--font-sans`.
- Site extraction proposes components as boxes with one line of text; lifting
  whole elements into vectors is phase 2's job.
- Omarchy themes: colours and the shell's spacing. Fonts, the bar layout and
  wallpapers come along untouched.
