# Git-native files and agent access (backlog, 2026-09-28)

**Status: approved for the backlog by the author on 2026-09-28. Not
scheduled.** It comes after the quick-win queue (QUICK-WINS.md, ending with
Browser View). The author will say when to start.

**Where it comes from:** a look at Elyx (elyx.design, by Sketch's
founders, private beta), whose pitch is readable, git-tracked design files
that agents work on directly. Don't copy its separate design language.
Take its three real advantages (reviewable diffs, agent access with the app
closed, and links between files) and build them on `.omai`.

**Today:** `.omai` is compact single-line JSON (`ProjectStore::write`): a
flat object list with UUID parent links, and images inline as base64 PNG.
Every `omastrator` CLI verb talks to the running app, and none works on a
file directly.

## A. Files that git and agents can read (small, about 2 days)
- **A stable, readable `.omai`:**
  - indented JSON, keys in a fixed order, objects nested as a tree, and
    defaults left out;
  - the same file type and version, so old builds read it;
  - saving twice gives identical bytes.
- **`omastrator file …`, with no daemon:** it links oma_core, oma_io and
  the renderer, and runs offscreen.
  - `inspect doc.omai [--page P] [--json]`: pages, artboards, the layer
    tree, and the components, tokens and fonts used.
  - `render doc.omai --artboard NAME --scale 2 -o out.png` (also SVG and
    PDF).
  - `check doc.omai`: missing fonts, colours that aren't a token, detached
    instances and empty artboards. It exits non-zero for CI.
  - `format doc.omai`: rewrites the file in the stable form, for pre-commit
    hooks.
- **An agent skill** in the Agent Skills format, shipped in the repo.
  `omastrator setup` offers to install it for Claude Code, Codex and
  Cursor. It teaches the file CLI when the app is closed, and the MCP tools
  when it's open.
- **UX:** none in the app.

## B. Git-native projects (medium, about 1 week; needs Pages)
- **A folder format, `Project.omai/`:** `document.json`,
  `pages/<page>.json`, `tokens.json` and `assets/<sha>.png`. Images leave
  the JSON, and merges touch only the pages that changed.
- **`omastrator file diff a b [--png dir]`:** objects added, removed or
  changed, by id and property. `--png` renders before and after with the
  changes outlined, for PR comments.
- **`omastrator file merge`,** a git merge driver: a three-way merge by
  object id. Edits to different objects never conflict, and a real
  conflict keeps both versions, marked.
- **UX/UI:**
  - File ▸ Save As… gets a Format menu: "Omastrator document" or
    "Omastrator folder (for git)".
  - When the file is in a repository, the status bar shows
    `⎇ main · 3 changes`. It opens File ▸ History: the commits for this
    file, with thumbnails. Picking one shows a visual diff in the canvas,
    with changes outlined and a before/after slider (reusing Live's
    Review changes sheet).
  - Ctrl+K gets "Compare with…" and "Restore this version".

## C. Linked libraries across files (medium, about 1 week)
- Components and tokens can come from another file by relative path
  (`../design-system.omai#Button`). The global Library (`src/System/Library`)
  gains linked-file entries, and a file watcher notices when a source
  changes.
- **UX/UI:**
  - Assets gets a Libraries section with an **Update** badge per changed
    source.
  - Review updates… shows each component before and after, with Accept and
    Accept all, as one undo step.
  - Instances show a link glyph in Layers. Hovering names the source, and
    "Go to main component" opens it in a tab.

## D. Flows and notes (after Browser View phase 2)
- A flow is a named sequence of artboards across pages, with labelled
  arrows (the trigger, such as "Tap Buy").
- Notes are Markdown on any artboard, page or component. Agents read both
  through `file inspect` and MCP.
- **UX/UI:**
  - Drag with Alt from one artboard's label to another to draw a flow arrow.
  - A Flows list sits under Pages in Layers. Clicking a flow steps through
    it, in the canvas and, later, in Browser View frames.
  - Notes is a folded section in Properties, and a Notes toggle shows
    markers on the canvas.

## Order and why
- A reuses what exists and makes Omastrator friendly to agents and CI
  straight away.
- B builds on Pages.
- C and D change the workflow the most, so they wait until the file work
  is solid.
- Everything stays MIT and open, which is the one thing Elyx, invite-only
  and closed, can't offer.
