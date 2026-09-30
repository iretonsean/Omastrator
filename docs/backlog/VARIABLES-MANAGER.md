# Variables and tokens manager, and a design system per project (backlog, 2026-09-29)

**Status: not scheduled. Nothing here is built.** The author asked for it on
2026-09-29 as a backlog item. Start with task 1: it's research, and it decides
the rest.

## The goal

Give Omastrator a manager for variables and tokens that is at least as good as
Figma's, and better where Figma is weak. It belongs to a **design system that
is kept per project**, not per document. Agent features are built into
managing it. Follow docs/VISION.md: the essentials are visible and the rest is
one step away (disclosure, the context menu, Ctrl+K). The designer stays the
author.

## What exists today

Read docs/DESIGN-SYSTEMS.md first. In short:

- **Tokens** of five kinds: colour, type, spacing, radius and shadow. They
  have slash-group names, **modes** (`DesignTokens.h`: `modes`,
  `addTokenMode`), references from paints and objects, and "Edit Token" as one
  undo step.
- **Components** with variants and instances.
- **Window ▸ Design System**, with the tabs Tokens, Components and Sources
  (`src/UI/DesignSystemPanel*`).
- **Sync** with:
  - the project's code (W3C `tokens.json`, Tailwind v4 `@theme`, Tailwind v3
    read only, CSS custom properties);
  - a global library;
  - any site (Extract);
  - Omarchy themes.

  Every push and pull goes through `SyncPlan` and `SyncConfirmDialog`.
- **Live in a Browser View** reads a page's tokens (`src/Live/Tokens.cpp`) and
  snaps edits to them. With Tailwind, an edit becomes a class swap.
- **The gap:** tokens live in the **document**. A project's documents don't
  share one system. The only shared store is the global library, which isn't
  tied to a project.

## Task 1: explore how to make Figma's variables manager better

Research first, then a short design proposal with options for the author to
pick from, as docs/PANELS.md did. **Check every claim about Figma against the
current Figma** (its help centre, release notes, and the app itself). The list
below is what to confirm, not settled fact.

1. **Record what Figma's manager does today:**
   - collections, groups and modes;
   - variable types: colour, number, string and boolean;
   - aliases (a variable that points to another);
   - scoping (where a variable may be used);
   - code syntax per platform (Web, iOS, Android);
   - extended collections for several brands, if they exist;
   - the table view, bulk editing, and search;
   - modes set per frame or page;
   - publishing to a library, and the REST API and plugin access.

   Take screenshots and note the keys.
2. **Collect the complaints.** Sources:
   - Figma's community forum and feature-request votes;
   - docs/research/unbuilt-features-2026-09.md and similar research;
   - the Tokens Studio plugin and why people install it;
   - design-system teams' posts on the subject.

   Complaints to check:
   - a cramped table;
   - no clear view of where a variable is used, and no way to find unused
     ones;
   - renames that break the code;
   - no history per variable;
   - no dependency graph for aliases;
   - no checks (contrast, spacing that isn't on the scale);
   - handoff to code that needs a plugin;
   - no formulas (for example, spacing as a multiple of a base).
3. **Compare** Figma, Tokens Studio, Penpot, Framer, Paper and Style
   Dictionary. For each one, note what it does better.
4. **Write the proposal.** For each weakness, say what Omastrator does
   differently. Include two or three layout mock-ups (artifacts, as with the
   dense panels). Mark what is "essential and visible" and what is "one step
   away".
5. **Decide with the author:**
   - which variable types to add beyond today's five kinds (number, string and
     boolean, as Figma has them);
   - whether "variables" and "tokens" are one concept or two.

## Task 2: a design system per project

- **Where it lives.** One design system per project folder, stored in the
  project, not only in each `.omai`. Options to decide:
  - a `design-system/` folder with `tokens.json` (W3C, already read and
    written) plus a components file;
  - the project's own token files as the only source of truth.

  Documents in the project **link** to the system, and don't copy it. See
  docs/backlog/GIT-NATIVE.md, section C, "Linked libraries across files".
- **Resolving the project.** Reuse what exists: `ProjectRegistry` (a site's
  origin to its folder), the Sources tab's project folder, and a document's
  own folder.
- **Documents without a project** keep today's per-document tokens.
  Documents with one can **Move to Project System**, which is a `SyncPlan`
  with a confirmation.
- **Several brands and themes:** modes, and collections that extend a base
  collection.
- **Offline and in git:** plain files that diff well, committed with the
  confirmation rule. Push to a remote never happens without the user.

## Task 3: the manager UI

To be shaped by task 1. The starting points:

- **A table:**
  - rows are variables, columns are modes;
  - groups fold;
  - inline editing;
  - multi-select for bulk edits;
  - drag to reorder;
  - Ctrl+K to reach any variable.
- **The side of a row, one step away:**
  - where it's used (documents, objects, code files and lines);
  - what it aliases, and what aliases it;
  - its history (from git);
  - its code name per platform.
- **Checks shown in place:**
  - contrast between text and background pairs, per mode;
  - values off the scale;
  - variables with no uses;
  - aliases that form a loop or point to nothing.
- **Formulas:** `spacing/lg = spacing/base × 3`, kept as a formula, so that
  changing the base updates the scale.
- **Modes to try on the canvas:** switch a frame or page to a mode, as Figma
  does. For a Browser View, the page's own theme (dark mode) follows.
- **Renames are safe:** a rename shows every code reference it will change,
  as a `SyncPlan`, before anything is written.

## Task 4: agent features in the manager

Follow docs/AI-ROADMAP.md:
- the app hands work to the default agent, and there's no bundled model;
- every AI change is a preview the designer accepts or discards;
- every file write goes through `SyncPlan`.

Ideas to shape:

1. **Build a system from what exists.** The agent reads the project's code,
   its documents and the live site (Extract). It proposes collections, a
   colour ramp, a type scale and a spacing scale, and names them.
2. **Tidy.** It finds near-duplicates (`#3f5a46` and `#3f5b46`), values that
   aren't on the scale, and unused variables, and proposes merges.
3. **Name with AI**, as the Layers panel does, following a naming convention
   the user picks.
4. **Make a mode:** "make a dark mode" or "a high-contrast mode", with
   contrast checked for every text and background pair.
5. **Explain and audit:** a plain report of where the system is inconsistent,
   across design and code.
6. **Keep design and code in step:** when code changes a token, the manager
   shows the difference and offers to take it. When a token changes in
   Omastrator, the agent updates code that the deterministic write-back can't
   update.
7. **From Live and Browser View:** an edit to the page that doesn't match any
   token offers "Make a token from this", or "Snap to the nearest token".

## Task 5: connect it to what's built

- **Live and Browser View** read the project system, not only the page's
  scan. Snapping and class swaps use it.
- **Components** bind to variables, and variants follow modes.
- **Exports:** Export for Screens, and Share, render in a chosen mode.
- **The agent's MCP tools** (docs/AI-DESIGN.md) get read and write tools for
  variables, and every write still goes through the confirmation.
- **Tests:** a temporary project folder and HOME for each case. No test
  touches the real desktop or a real remote (AGENTS.md).

## Open questions for the author

- Variables and tokens: one concept (Figma calls them variables) or two?
- Is the project's system stored in Omastrator's own folder, or only in the
  project's token files?
- Does the global library stay, as a set of starting kits for new projects?
- How far do formulas go: arithmetic only, or colour functions too (mix,
  lighten, or OKLCH steps)?
