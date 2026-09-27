# Handoff: design systems (ANYWHERE.md phase 3)

Branch `worktree-agent-a9d2d6b27bac641c1`. The design and every decision are in
[../DESIGN-SYSTEMS.md](../DESIGN-SYSTEMS.md).

## Done

- [x] Tokens in the document: colour, type, spacing, radius, shadow; modes
  (light/dark, any names); references from paints (fill, stroke, stacks),
  stroke weights, live corners, text, text styles and group gaps. A token edit
  or mode switch is one undo step; a value changed by hand drops its link.
- [x] Components (also QOL P2-6 Symbols): Make Component, instances with
  overrides (fill, stroke colour, text, visibility) keyed by layer name,
  variants (property → value sets), swap variant, detach, reset overrides;
  editing the component updates every instance in the same step. Duplicating
  a component makes an instance; deleting one detaches its instances.
- [x] `.omai` version 4 with migration tests (version 3 opens with no system,
  version 5 is refused).
- [x] `src/System` (`oma_system`): W3C tokens.json, Tailwind v4 `@theme`,
  Tailwind v3 config (read), CSS custom properties with a dark mode; the
  global library in `$XDG_DATA_HOME/omastrator/libraries`; site extraction
  over CDP; Omarchy themes (read, new folder, `omarchy theme set`).
- [x] The confirmation rule: every push/pull is a `SyncPlan`; only
  `SyncConfirmDialog` makes the `Confirmation` that `SyncRunner::execute`
  needs. The dialog lists where it publishes, every file with its full path
  and a line summary, the repository, branch and commit message, the command,
  the in-app change, and a dry-run diff. Cancel is the default. A file changed
  after the preview stops the whole run.
- [x] UI: Window ▸ Design System (Tokens / Components / Sources), Object ▸
  Components (Ctrl+Alt+K, Ctrl+Alt+B), a Component section in Properties
  (variant menus, Reset, Detach), Ctrl+K (Apply Token, Place Component, Swap
  Variant), the in-app task bar (Make Component / Detach on groups), and the
  floating bar (Extract Design System on pages; Make Component and Design
  System on art).
- [x] Tests: `DesignSystemTests` (model), `DesignSourcesTests` (formats on
  temporary projects, library and themes in a temporary HOME, headless
  Chromium fixture), `DesignSystemUiTests` (dialog blocks writes, git commit
  on a temporary repo, fake omarchy).

## Left / next steps

- Token links aren't shown next to the fill and stroke rows in Properties
  (they're in the Design System panel and Ctrl+K); a small token chip there
  would help.
- A shadow effect on objects, so shadow tokens can apply to art.
- Instances only override paint, text and visibility; nested instances are
  one level deep.
- Site components are boxes with one line of text until phase 2's Lift can
  lift whole elements.
- The floating bar's Design System opens the window's panel on the overlay;
  a picker inside the bar itself would keep you on the surface.
- Not tried by hand on the real desktop (no theme was switched and no real
  repository touched, by design).
