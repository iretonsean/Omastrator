# Motion in Browser View (design, 2026-09-30)

The live page in a Browser View is the artboard for motion. The designer asks
the agent for an animation, sees it as tracks on a timeline, tunes it with
designer controls, and saves it to code. The code stays the only source of
truth: the agent writes plain CSS (or the project's own stack) into the
project, and Omastrator reads that code back and edits it.

This builds on Live inside the frame (LIVE-IN-FRAME.md): Edit Page, the element
bar, Live's undo, write-back, Save, Deploy and Build It. Nothing here is built.

The three flows (the approved prototype):

1. **Animate an element:** Select, Ask, Tune, Ship. Animate in the element bar,
   a prompt, a timeline under the frame, the Motion inspector, Save to code.
2. **From a blank frame:** an empty Browser View offers Type an address,
   Generate a page… and Build It from a canvas frame.
3. **Animate a selection together:** Shift-clicked elements animate as one
   group, with one prompt, one group track and per-element offsets.

## 1. The model

**Document state: none.** Motion lives in the project's code. The `.omai` file
keeps only what it keeps today (the frame, its address, its size). The timeline,
the playhead and the inspector's values are view state of the frame's Live
session, and they are gone when Live stops. No motion edit is a document undo
step, for the reasons LIVE-IN-FRAME.md section 4 gives for page edits.

**Project code: everything else.** Durations, easings, staggers, keyframes,
triggers and the reduced-motion rule are CSS (or the stack's own files) in the
project folder, on disk and in git.

**Reading motion back.** The page's motion is read in the page, by the
overlay, through the Web Animations API:

- `document.getAnimations()` lists every running animation: CSS animations
  (with `animationName`), CSS transitions (with `transitionProperty`), and
  Web Animations made by scripts (the Motion library uses them).
- For each one, `effect.getTiming()` gives delay, duration, iterations, fill
  and easing, `effect.getKeyframes()` gives the keyframes with their offsets
  and values, `effect.target` gives the element, and `timeline` says whether
  it's the document timeline, a `ScrollTimeline` or a `ViewTimeline`.
- `pause()`, `currentTime = t` and `play()` pause, seek and play.

Why the overlay and not the DevTools Protocol's `Animation` domain: the overlay
is already the page's single seam (`window.__oma`, `omastratorSend`); the
`Animation` domain reports only animations created after `Animation.enable`,
so a load animation needs a reload before it's seen; and the Web Animations
API gives the same list, timing and keyframes on the page's own thread. The
DevTools Protocol is used where only it can do the job:

- **`CSS.forcePseudoState`** (with `DOM.enable`, `CSS.enable` and
  `DOM.querySelector` for the node) forces `:hover`, `:focus` and `:active`,
  so a Hover trigger's transition runs and can be seen and scrubbed without a
  real pointer on the element. It's released when the timeline closes.
- **`Animation.setPlaybackRate`** is not used. The timeline pauses
  animations one by one, so the page keeps its own clock for everything else.

`__oma.motion` is a new part of `overlay.js`:

- `list()` returns the tracks (section 2), with each animation's selector
  (`selectorFor`), property list, start, duration, easing, keyframes, timeline
  kind and source (`css-animation`, `css-transition`, `script`).
- `hold()` pauses every animation and keeps pausing new ones (it listens to
  `animationstart` and `transitionrun`, and re-lists once per animation
  frame while held). `release()` plays them again from where they are.
- `seek(ms)` sets `currentTime` on every held animation, relative to each
  one's start on the timeline.
- `trigger(kind, selector)` starts a Click trigger's animation by doing what
  the code says (it adds the class the agent's rule names, section 4).

**Forms it understands deterministically:**

- CSS transitions;
- CSS `@keyframes` animations;
- scroll-driven animations (`animation-timeline: scroll()` or `view()`);
- Web Animations made by scripts: listed, paused and seeked, but not written
  back (they're code).

**Forms only the agent edits:**

- JavaScript libraries that drive their own clock with `requestAnimationFrame`
  (GSAP, anime.js, Framer Motion's JavaScript springs). They don't appear in
  `getAnimations()`. If `window.gsap` exists, `hold()` pauses
  `gsap.globalTimeline` and `seek` seeks it, and the timeline shows one track,
  "GSAP timeline", that can be scrubbed but not tuned. Other libraries show as
  "Motion from a script (not shown here)" in the inspector, with Ask….
- View Transitions (`document.startViewTransition`, `@view-transition`). They
  run between two states of the page and aren't in `getAnimations()` until one
  starts. The timeline lists them only while one runs, read-only. Tuning them
  goes to the agent.

## 2. The timeline and scrubbing

**Where it is.** The timeline is a panel docked under the canvas (Window ▸
Timeline), and it follows the Browser View in Edit Page. It opens by itself
after Animate and when the selected frame's page has motion; it closes with
its × and stays closed until the next Animate. It has two tabs, **Timeline**
and **Code** (section 3), and a header: Play/Pause, the time ("1.40 s / 2.06
s"), the trigger ("· On load"), **Record MP4** and **Save to code**.

**Tracks.** One row per element and property group, as the prototype draws
them:

- The label is the element's short selector ("h1 .word × 5", "p.lede",
  "a#primary", "article × 3"), and the sub-label its properties ("opacity,
  translate").
- Animations with the same `animationName` (or transition property list) on
  siblings of one parent merge into one row with one bar per element, drawn
  as thin stacked bars. That is how a stagger reads at a glance.
- Bars are placed by `delay` and sized by `duration × iterations`
  (an infinite animation draws one iteration and a ↻ mark).
- A group (section 5) is one row that opens into one row per element.
- Clicking a row selects its elements on the page (Edit Page's selection),
  and the inspector follows.

**Pausing and seeking.** Opening the timeline calls `__oma.motion.hold()` in
the frame's tab (the pool's tab, through `LiveFrames::run`). Every animation
pauses where it is. The playhead then drives `seek(ms)`:

- **Scrub:** dragging the playhead or the ruler seeks. Seeks are coalesced to
  one in flight: the next is sent when the screencast frame for the last one
  has arrived, so a fast drag never queues work behind the picture.
- **Play:** Omastrator advances the playhead itself at the screencast's pace
  (the time since play started, sent as a seek per picture), so Play and the
  scrub show exactly the same frames and Record (section 8) can use the same
  path. **Loop** (a toggle beside Play) starts again at the end.
- **Replay** at 0 is Play from 0. A load animation is seen from its start this
  way, without reloading the page.
- The frame may be paused (off screen, another page). The timeline resumes it
  while it's open, as Browse does.

**Scroll-driven motion.** For an animation on a `ScrollTimeline` or
`ViewTimeline`, time is scroll position:

- The ruler's unit becomes the page's scroll offset in px, with the element's
  `animation-range` drawn as the bar.
- Seeking scrolls the page (`window.scrollTo`), which is what drives the
  animation. The wheel over the frame in Edit Page already scrolls the page,
  so **the frame's scroll drives the playhead**.
- A page with both kinds shows the scroll-driven rows under a divider,
  "On scroll", with their own ruler.

**The picture while scrubbing.** Phase 3's screencast carries it:

- The frame whose timeline is open counts as the browsed frame, so it gets
  every screencast frame (no 33 ms gap).
- A seek changes paint, not layout or the page's size, so the stale picture
  rule (a picture is stretched only when it depicts the frame's size) is never
  hit by scrubbing. A held breakpoint preview still works: the timeline scrubs
  the page at 390 as it is.
- The selection boxes follow the animated elements, since the overlay's
  geometry report runs once per animation frame.

**Closing the timeline** calls `release()` and releases forced pseudo-states.
Stop Live, leaving Edit Page, reset and closing the document close it too.

## 3. Editing and writing back

**The Motion inspector** is a floating panel (Window ▸ Motion), opened with the
timeline and following its selection. Per PANELS.md, Properties gains no
section: motion is a property of the page's code, not of a document object. It
holds, top to bottom:

- the selection's name ("h1#headline · 5 words");
- with no motion: "No motion on this element yet." and **Animate…**, with the
  preset line "Reveal, Fade up on scroll, Lift on hover, Stagger";
- **Starts:** Load, Scroll, Hover, Click (a segmented control);
- **Easing:** Soft out, Spring, Linear, the tokens of kind `ease/*`, and a
  curve editor with two handles and the value shown as text
  ("cubic-bezier(0.16, 1, 0.3, 1)");
- **Duration** (per element, or "per word" for a split headline) and
  **Stagger**, as `NumberField`s in ms;
- **Motion tokens, from the project's design system:** the tokens the motion
  uses ("duration/reveal 480ms", "ease/reveal soft out", "stagger/words
  60ms"), each editable;
- **Also write a reduced-motion version (prefers-reduced-motion)**, on;
- keyframe values, one step away: a disclosure, "Keyframes", with each
  keyframe's offset and its properties (opacity, translate, scale, rotate).

**The Code tab** shows the lines the motion lives in, read from the project's
files (not from the page), with the lines a pending edit changes marked.
"Live code. Edits on the timeline and in the inspector rewrite these lines."
It is read-only; a click on a line opens the file with `xdg-open`.

**Every change is a preview first.** Scrubbing a field calls the page, and
releasing it records one Live edit, exactly as the element bar does:

- a token or any other custom property: an inline custom property on `:root`
  (or on the element whose rule declares it), through today's
  `applyResolved`;
- a keyframe value: `effect.setKeyframes()` on the held animations;
- a duration, delay or easing that isn't a token: `effect.updateTiming()`.

The page follows at once, and the timeline redraws from `list()`.

**What `WriteBack` writes with certainty.** Today's rule (`WriteBack::plan`) is
"text, a custom property declared once, and a Tailwind class swap". Motion adds
two rules, both plain text matches like the others:

1. **A scoped custom property:** `--name` inside the rule whose selector text
   is the edit's selector, where that rule occurs once. This is what makes
   per-element values (`--i`, `--delay-extra`) writable, since `--i` is
   declared many times.
2. **A value inside a named `@keyframes` block:** `@keyframes <name>` occurs
   once across the style files, the block's braces balance, and the keyframe
   selector (`from`, `to`, `40%`) and the property occur once in it. The
   declaration's value is replaced.

The agent's output contract (section 4) puts every tunable value where these
rules and today's custom-property rule reach it. So, for motion the agent
wrote, **every inspector edit except Starts is certain**:

| Change | How it's written |
|---|---|
| Duration, stagger, easing (a token) | the token's custom property (today's rule) |
| Easing preset or curve | the `--ease-*` token's value |
| Duration or delay of one element | a scoped custom property |
| Group order, per-element offset | scoped `--i` and `--delay-extra` (section 5) |
| A keyframe value | the named `@keyframes` block |
| Reduced motion off, then on again | the marked block's `@media` rule is removed or put back (below) |

**What goes to the agent:**

- **Starts** (Load, Scroll, Hover, Click). A trigger changes selectors, adds
  `animation-timeline`, or adds a class toggle in markup or script. It is
  previewed on the page (the overlay swaps the held animations' timeline for
  Scroll, forces `:hover` for Hover, adds the class for Click) and saved as an
  edit the agent finishes.
- Motion the agent didn't write: a value that isn't a custom property, a
  transition declared inline in a component, a `@keyframes` name that occurs
  twice.
- Everything in script-driven motion.

These are Live edits like any other, so Save's existing split applies: the
certain part is written and the rest is handed to the agent with a
description ("Change #headline .word to start on scroll into view").

**The marked block.** The agent writes each motion between two comments:

```css
/* omastrator:motion headline-reveal */
…
/* omastrator:motion end */
```

Omastrator uses the markers for two things only: the Code tab shows the block,
and the reduced-motion toggle finds the block's `@media
(prefers-reduced-motion: reduce)` rule. Turning the toggle off removes that
rule (kept in Live's undo); turning it on puts the same text back. A block that
has lost its markers still works; its edits just fall back to the rules above.

**Undo** is Live's own history (LIVE-IN-FRAME.md section 4): each released
field is one `UndoStep`, a group change (order, stagger) is one grouped step,
and Ctrl+Z in Edit Page reaches it. `__oma.restore` gains the two new preview
kinds: it puts back the custom property, the keyframes or the timing it saved.
Scrubbing the playhead is not an edit and has no undo.

## 4. Ask and generate

**Animate** is a new element bar control, after Ask…, on the user's own site
only (as Ask…). With several elements selected it reads **Animate together**.
It opens the Animate sheet under the bar:

- a field, "Describe the motion", prefilled with nothing;
- suggestions as chips: "Reveal word by word on load", "Fade up as it scrolls
  in", "Lift on hover", "Stagger the cards" (a chip fills the field);
- "Your default agent", **Cancel** and **Generate**;
- the line "Claude writes the motion as real CSS in your project, on a
  branch. You preview it here before anything is saved."

**The context the agent gets** (`AgentWork::prompt`'s `Brief`, extended):

- the selection: each element's `info()` (selector, tag, classes, text, box,
  styles) and its HTML, as today's `elements`;
- the page's tokens, including the motion tokens (section 6), and the file
  they live in;
- the stack, read from the project (`package.json` dependencies, a Tailwind
  v4 `@import "tailwindcss"`, Astro), and the style file the project already
  uses;
- the existing motion on those elements, from `__oma.motion.list()`, with the
  `@keyframes` names already used on the page;
- the frame's picture as the screenshot, and its width and breakpoints;
- the instruction.

**The output contract** (added to the prompt, and checked by Omastrator after
`agentDone`):

- Plain CSS in the project's own style file (for Tailwind v4, the CSS file
  with `@theme`). No new dependency, no JavaScript unless the trigger is
  Click, and then the smallest listener that toggles one class.
- Every tunable value is a custom property: durations `--duration-<name>`,
  easings `--ease-<name>`, staggers `--stagger-<name>`, declared in `@theme`
  (Tailwind v4) or `:root`.
- A `@keyframes` name that is new to the project, prefixed with the site's
  short name (`nl-rise`), used by one rule per selection.
- Per-element timing through `--i` and, when set, `--delay-extra`:
  `animation-delay: calc(var(--i) * var(--stagger-words) + var(--delay-extra, 0ms))`.
- Markup changes only where the motion needs them (splitting a headline into
  `<span class="word" style="--i: 0">`), and nothing else in the markup.
- The block markers (section 3).
- A reduced-motion rule, unless the designer turned the toggle off in the
  sheet's ⋯ (section 7).
- It ends with `agentDone` and a one-line summary, as today, and doesn't
  commit, push, deploy or start a dev server.

After `agentDone`, Omastrator checks the contract (the markers, the tokens, one
`@keyframes` per name). A result that breaks it is still shown, with a line in
the inspector: "Some of this motion isn't tunable here. Ask to fix it, or edit
it in code."

**Preview before accept.** Today `liveAgentDone` writes the agent's change into
the project at once and records a Review. Animate must not: the prototype's
promise is "Preview only until you save". So an Animate run stops at the
worktree:

1. The agent works in `AgentWork`'s worktree, as every Live run does.
2. On `agentDone`, Omastrator collects the worktree's diff but doesn't apply
   it. It serves the worktree instead: `DevServers::acquire(worktree)`, with
   `node_modules` as a hard-linked copy of the project's (`cp -al`, so no
   install and no network), or the static server for a plain HTML project.
3. The frame's tab goes to the preview server, at the same path. The dev swap
   (`toTabUrl`, `toDocumentUrl`) keeps the document's address unchanged, as it
   does for the project's own dev server. The bar's "dev" tag reads
   "preview", and its tooltip names the branch.
4. The timeline opens, holds the new motion and plays it from 0. The toast
   says "Claude wrote the motion into src/style.css and index.html. Preview
   only until you save."
5. Tuning in the preview is Live edits, as in section 3. They are pending
   edits of the project, as any.
6. **Save to code** (or Save, or Deploy) applies the worktree's diff to the
   project through the existing path (`WriteBack::apply`, the Review "Animate:
   <selection>"), then writes the pending edits on top, then commits (and
   pushes, as Save does today). The frame goes back to the project's dev
   server and the preview server is released.
7. **Discard** (in the toast, the inspector, and Esc while the sheet is open)
   releases the preview server, removes the worktree, drops the edits made in
   the preview, and loads the project's page again. Nothing was written.

Only one preview per project at a time; a second Animate while one is open
asks "Keep or discard the motion you're previewing first?" with Keep, Discard
and Cancel.

**Generate a page** (an empty frame):

- An empty Browser View today says "No page yet." It gains three actions in
  the frame, under that line: **Type an address** (focuses the address
  field), **Generate a page…** and **Build It from a canvas frame** (shown
  when the frame has design children; otherwise its line explains "Design it
  with vectors and text first; Claude turns the frame into code.").
- The sheet asks "What should it be?" (with chips: Landing page, Portfolio,
  Pricing page, Docs home), **Built with** (Vite + Tailwind, Plain HTML,
  Astro), and **Project folder** (`~/Projects/<name>`, a folder chooser, or an
  existing project from `ProjectRegistry::suggest`).
- **Templates.** Omastrator ships one small template per stack, in code, as
  text: Vite + Tailwind (`index.html`, `src/style.css`, `package.json`,
  `vite.config.js`, `.gitignore`), Plain HTML (`index.html`, `style.css`),
  Astro (`src/pages/index.astro`, `src/styles/global.css`, `package.json`,
  `astro.config.mjs`, `.gitignore`). The project's design system tokens (the
  document's, if it has any) go into the template's token file.
- **The agent writes into a staging folder**
  (`$XDG_CACHE_HOME/omastrator/generate/<stamp>`), with the template already
  in it, and access to that folder only. It ends with `agentDone`.
- **Then the confirmation.** A `SyncPlan` shows "Create a new project?": Creates
  (the folder), Writes these files (every file with "new, 38 lines"), Commits
  to ("A new git repository, branch main: “First page from Omastrator”.
  Nothing is pushed."), Then runs (`npm install`, then `npm run dev`; or "a
  static server for the folder"), and In Omastrator ("Browser View 1 shows the
  page from its dev server. One undo step."). Cancel is the default.
- **On Confirm:** the files are moved into the folder, `git init -b main` and
  the commit run (the repository's identity, as `SyncRunner` commits today),
  the origin `http://localhost:<port>` is not registered (the dev server's own
  origin is enough), `DevServers::acquire(folder)` starts the server, and the
  frame's address is set to it (one "Change URL" undo step). Never a push, a
  remote or a deploy.
- An existing project folder is refused for Generate ("This folder isn't
  empty. Generate a page writes a new project.").
- **Activity.** The Live panel's frame section shows an Activity list while it
  runs, one line per step with ✓, … or ·: "Writing the page from your
  description", "Using your design system tokens for colour and type", "npm
  install", "Starting the dev server on localhost:5173", "Committed “First page
  from Omastrator” to main". The frame shows the current line over its empty
  page.

This order (the agent writes first, then the plan lists the real files) is not
the prototype's (the plan first). The author chose it on 2026-09-30 (section 11).

**Build It from a canvas frame** is the existing Build It. From an empty frame
it needs a folder, so it asks for one as Hand to Agent does on a site that isn't
yours; with a new folder, the template and the confirmation above come first.

## 5. Groups

**A multi-selection is a group.** Shift-click in Edit Page selects several
elements (built). Animate together sends them in one Brief with one
instruction. The prototype's numbers on each element (the pick order) are
drawn by the canvas from the selection's order.

**How a group maps to code** (the output contract):

```css
/* omastrator:motion beans-cascade */
@theme { --duration-cascade: 640ms; --stagger-cascade: 140ms; --ease-cascade: cubic-bezier(0.25, 1, 0.5, 1); }
@keyframes nl-cascade { from { opacity: 0; translate: 0 44px; rotate: -3deg; } }
#guji  { --i: 1; }
#huila { --i: 2; }
#nyeri { --i: 0; }
.bean-card {
  animation: nl-cascade var(--duration-cascade) var(--ease-cascade) both;
  animation-delay: calc(var(--i) * var(--stagger-cascade) + var(--delay-extra, 0ms));
  animation-timeline: view();
}
@media (prefers-reduced-motion: reduce) { .bean-card { animation: none; } }
/* omastrator:motion end */
```

One rule animates the group; each element carries only its index. If the
elements have no stable selector, the agent adds an id or a class (a markup
change the contract allows).

**Group controls** (the inspector with the group row selected, "Group · 3
cards"):

- **Order:** As picked, Left → right, Centre out, Shuffle. Omastrator computes
  the indices (from the pick order, from each element's box, from the distance
  to the group's centre, or a shuffle seeded once and kept), and writes each
  `--i`: certain, by the scoped custom property rule.
- **Stagger** and **Duration:** the group's tokens.
- **Effect:** Rise, Grow, Flip, from the group's `@keyframes` block. A preset
  replaces the keyframes' declarations (certain); anything else is Ask….

**One element's own timing.** Open the group, pick one element's row: "Extra
delay for this card" writes `--delay-extra` in its index rule; **Use the group's
timing** removes it. The others keep the group's timing.

## 6. Motion tokens

Three new kinds, as the prototype names them:

| Kind | W3C `tokens.json` | Tailwind v4 `@theme` | CSS custom properties |
|---|---|---|---|
| `duration/*` | `$type: "duration"` (`{"value": 480, "unit": "ms"}`) | `--duration-*` | `--duration-*` |
| `ease/*` | `$type: "cubicBezier"` (`[0.16, 1, 0.3, 1]`) | `--ease-*` (Tailwind's own namespace) | `--ease-*` |
| `stagger/*` | `$type: "duration"` in a `stagger` group | `--stagger-*` | `--stagger-*` |

- `TokenFiles` reads and writes them (W3C `kindFor` stops skipping `duration`
  and `cubicBezier`; Tailwind and CSS read the three prefixes). An easing
  keyword (`ease-out`) is kept as text; `linear()` is kept as text and shown as
  "Custom".
- `Live::TokenSet` gains the three groups, from the page scan, so the
  inspector's fields snap to the scale as lengths do, and show the token name as
  the unit.
- `Document/DesignTokens` gains `TokenKind::duration` and `TokenKind::easing`
  (stagger is a duration token in a `stagger` group). The Design System
  panel's Tokens tab lists them, and Sync pushes and pulls them with the rest.
- Tailwind v4 check, to do in phase B: that a `--duration-*` and a
  `--stagger-*` in `@theme` are emitted as variables in the built CSS without a
  utility namespace, and that `--ease-*` generates `ease-*` utilities (which
  it does).

**Before and after the variables manager.** This lands before it (it isn't
scheduled). The three kinds go into today's model, with no new concept. When
the manager comes, they are its first kinds beyond today's five, and the
manager's per-project system is where they live; nothing here stops that,
since the source of truth is the project's token file in both cases.

## 7. Reduced motion and accessibility

**On by default.** Every motion the agent writes has a `@media
(prefers-reduced-motion: reduce)` rule in its block. For entrance motion it's
`animation: none` (the element shows in its final state, since keyframes use
`from` only and `both` fill); for a loop, the loop stops; for hover, the change
stays and the movement goes (`transition-property` keeps colour and opacity,
drops transforms).

**What the inspector shows:**

- the toggle, "Also write a reduced-motion version (prefers-reduced-motion)",
  on;
- beside it, **Preview reduced**: the frame's tab emulates the setting
  (`Emulation.setEmulatedMedia` with `prefers-reduced-motion: reduce`), and
  the timeline shows what's left ("No motion when reduced" or the remaining
  tracks);
- a warning when the page's motion has no reduced rule at all (the site's own
  motion, not the agent's): "This motion plays for people who asked for less
  motion." with Ask… to add one.

The Animate sheet's ⋯ can turn the rule off for one run; the inspector's toggle
does the same afterwards (section 3). Nothing in Omastrator's own UI animates
because of this feature.

## 8. Recording

**Record MP4** in the timeline header (and **Record GIF…** one step away, in
its ⋯) records the frame, not the screen:

1. A save dialog first ("northlight-hero.mp4" in `~/Videos`, or the last
   folder). Nothing is written before Save.
2. The timeline holds the motion and plays it by seeking, one step per frame
   at a fixed 30 fps (60 in the ⋯). For each step it waits for the screencast
   frame after the seek, so the file is smooth whatever the machine's load.
   Scroll-driven motion records a scroll from the range's start to its end.
3. The frames (the screencast's JPEGs, at the frame's size on screen, up to
   2560 px) are piped to `ffmpeg -f image2pipe -framerate 30 -i - -c:v
   libx264 -pix_fmt yuv420p -movflags +faststart <file>`. A GIF uses
   `palettegen` and `paletteuse` in two passes on the same frames, from a
   temporary folder.
4. The header shows "Recording…" with Stop (and Esc). Stop ends the file
   where it is; an ffmpeg failure removes the partial file and says "Couldn't
   record: <ffmpeg's last line>".
5. The toast says "Recorded 2.1 s · northlight-hero.mp4", and a click opens
   the folder.

**Without ffmpeg** (`OMASTRATOR_FFMPEG`, else `ffmpeg` on PATH): Record MP4 is
shown but disabled, with the tooltip "Recording needs ffmpeg. Install it with
sudo pacman -S ffmpeg." Its ⋯ keeps **Save Frames as PNG…**, which writes the
same frames as a numbered PNG sequence into a new folder through Qt. No other
encoder is bundled.

## 9. Escape hatches

- **Stop and Esc during generation.** The Animate sheet's Generate becomes
  "Writing…" with Stop, and the sheet lists the agent's steps (✓, …, ·). Stop,
  Esc, or Cancel ends the run (`stopLiveJob`), removes the worktree, and leaves
  the project and the page as they were. Generate a page's Stop does the same
  to the staging folder. Reset (`design reset`) stops both, as it stops any
  wait today.
- **Discard for a preview.** An Animate preview is discarded from the toast,
  the inspector, Esc in Edit Page (the first Esc discards the open sheet, the
  second leaves Edit Page), Stop Live, reset and closing the document. A
  discarded preview leaves nothing on disk but Omastrator's worktree folder,
  which is removed.
- **Nothing written before Confirm or Save.** Tuning is Live edits, written
  only by Save, Save to code or Deploy. An Animate result is written only by
  Save to code. A generated page is written only after the plan's Confirm. A
  recording is written only to the file its save dialog named.
- **Nothing grabs the keyboard.** The timeline and inspector fields take focus
  on click only, and Enter or Esc hands the keys back to the canvas, as the
  element bar's do. No key reaches the page.

## 10. Build order

Each phase is its own branch off the previous one and merges on its own. Every
Chromium test `QSKIP`s without `Browser::executable()`, loads only
`tests/Live/fixtures` through `StaticServer` or the vite-tailwind fake dev
server, runs with temporary `XDG_*`, `HOME` and `OMASTRATOR_RUNTIME_DIR`, uses
`FakeAgents.h` and the fake `OMASTRATOR_OMARCHY`, and never reaches the network.

**A. The read-only timeline** (`feat/motion-a`).

- `__oma.motion` (`list`, `hold`, `release`, `seek`), `LiveSession::motion…`
  calls on the pool's thread, the snapshot's `motion` JSON.
- The Timeline dock: tracks, merging siblings, the ruler, the playhead, scrub
  with one seek in flight, Play, Loop, Replay; scroll-driven rows and seeking by
  scroll; `CSS.forcePseudoState` for a Hover row; the GSAP track.
- The Motion inspector read-only (values shown, not editable), and the Code tab
  (the block read from the files).
- A fixture, `tests/Live/fixtures/motion/`: a headline split into words with a
  `@keyframes` stagger, a hover transition, a `view()` scroll animation, a
  Web Animation made by a script, and a marked block.
- Tests: `MotionTimelineTests` (Chromium): `list()` finds each kind with its
  timing and keyframes; `hold()` pauses a load animation and one started later;
  `seek(240)` sets the computed opacity the keyframes give at 240 ms; seeking a
  scroll row scrolls the page; the frame's picture changes after a seek (the
  pixel test `LiveFramePictureTests` uses); release plays on.
  `MotionTimelineUiTests`: rows merge per parent, a row click selects its
  elements, Esc leaves Edit Page and releases.

**B. Inspector edits, write-back and motion tokens** (`feat/motion-b`).

- Editable inspector fields and the curve editor; previews through
  `applyResolved`, `setKeyframes` and `updateTiming`; `__oma.restore` for them;
  Live undo.
- `WriteBack`'s two rules (scoped custom property, `@keyframes` value) and the
  reduced-motion block toggle.
- The three token kinds in `TokenFiles`, `Live::TokenSet` and `DesignTokens`,
  and the Tokens tab.
- Tests: `WriteBackTests` (extended): a scoped `--i` written when its rule
  occurs once and left for the agent when twice; a keyframe value written in
  the named block and refused in a block that occurs twice or doesn't balance.
  `DesignSourcesTests` (extended): `duration` and `cubicBezier` round-trip
  through W3C, Tailwind v4 and CSS, keeping unknown keys. `MotionEditTests`
  (Chromium): a duration scrub previews and records one edit; Ctrl+Z restores
  the keyframes; Save writes the token and the scoped `--i` into a temporary
  git repository, and nothing else changes.

**C. Animate through the agent, and reduced motion** (`feat/motion-c`).

- The Animate control and sheet, the extended Brief and the output contract,
  the contract check, the worktree preview server with `cp -al`
  `node_modules`, the "preview" tag, Save to code, Discard; Starts (trigger)
  previews and their agent edits; Preview reduced.
- Tests: `AnimateTests` (Chromium, fake agent): the prompt carries the
  selection, tokens, stack and existing motion; a fake agent that writes a
  marked block leaves the project unchanged until Save to code, then one
  Review and one commit hold it; Discard leaves `git status` clean and removes
  the worktree; Stop during Writing… ends the run and removes the worktree;
  a second Animate asks first; the preview frame's document address stays the
  production one. `ReducedMotionTests` (Chromium): the emulated media leaves no
  running animation for the fixture's block; the warning shows for motion
  without a rule.

**D. Groups and multi-selection** (`feat/motion-d`).

- Animate together; pick-order numbers; the group row and its children; Order,
  Stagger, Duration, Effect; one element's extra delay.
- Tests: `MotionGroupTests` (Chromium): each order writes the expected `--i`
  values (Centre out from boxes, Shuffle stable across a reload); an extra
  delay moves one element's start and no other's; Use the group's timing
  removes it; a group change is one undo step.

**E. Generate a page in an empty frame** (`feat/motion-e`).

- The empty frame's three actions, the sheet, the stack templates, the staging
  folder, the `SyncPlan`, `git init` and the commit, `DevServers::acquire`,
  Activity.
- Tests: `GeneratePageTests` (fake agent, `SyncConfirmDialog::setResponder`,
  a fake `npm` on PATH that serves the folder with the static server): Cancel
  writes nothing and removes the staging folder; Confirm creates the folder, a
  repository with one commit and no remote, and the frame shows the page from
  the dev server; the plan lists every file with its line count; a non-empty
  folder is refused; Stop during writing leaves nothing. Each template builds a
  plan with the expected files (no agent).

**F. Record MP4** (`feat/motion-f`).

- Record MP4 and GIF, the save dialog, stepped seeking, the ffmpeg pipe, Stop,
  Save Frames as PNG….
- Tests: `MotionRecordTests` with `OMASTRATOR_FFMPEG` pointing at a fake that
  records its arguments and counts the JPEG frames on stdin: 2.0 s at 30 fps
  sends 60 frames; Stop ends early and the file is kept; a failing fake removes
  the partial file; with no ffmpeg, Record is disabled and PNG frames are
  written. One test with the real ffmpeg, skipped without it, checks the file
  with `ffprobe`.

## 11. Decided by the author (2026-09-30)

The author took the recommended answer to each open question.

1. **Generate a page: the agent writes first.** The agent writes into a staging
   folder, then the `SyncPlan` lists the real files, and Confirm creates the
   project. The prototype's steps stay; only Confirm comes after "Writing the
   page" (section 4).
2. **The Animate preview runs a second dev server on the worktree,** with a
   hard-linked `node_modules`, and one preview per project at a time
   (section 4).
3. **The timeline is a panel docked under the canvas, and the Motion inspector
   is a floating panel.** Properties gains no section (sections 2 and 3).
4. **Record is stepped,** frame by frame at 30 fps (60 in the ⋯). Motion that a
   script drives with its own clock isn't recorded unless it can be seeked
   (GSAP's global timeline can) (section 8).

## 12. As built: phase E, Generate a page (2026-09-30)

Built on `feat/motion-e`, off `feat/motion`. Section 4 holds the design; this
section lists what the build decided or changed.

**Files.** `Live/PageTemplates` (templates, staging, collect), `System/PagePlan`,
`UI/GeneratePageSheet`, `UI/BrowserViews+Generate.cpp`,
`UI/AgentBridge+Generate.cpp`, `AgentWork::pagePrompt`, and
`Canvas/EditorCanvas+EmptyFrame.cpp`. Tests: `tests/UI/GeneratePageTests.cpp`.

**Changes to shared types.** `BrowserViewHost` gains `Action::generatePage` and
`empty()` (what an empty frame offers). `SyncPlan` gains `destinationLabel`
("Creates"), `runsAfter` and `GitTarget::create`. The dialog shows them. The
runner makes the repository (`git init`, then `HEAD` on `main`) after it writes the
files and before it commits.

**The agent's run.** The agent starts in the staging folder with project access
and the template already written. It is not a git worktree, and it uses no branch.
`AgentBridge::generatePage` files the run by request id, and `agentDone` answers it.
The bridge owns the staging folder until the run says it wrote something. Stop,
Esc, reset and a run that ends without an answer remove it. A stopped run removes it
after the agent process has ended. An agent in a terminal cannot be stopped from
here: the folder goes at once, and a late `agentDone` finds no task.

**Stop is this run's.** The frame's generation keeps the agent's request id, and the pill, Esc, the panel's Stop, reset and
closing the document stop that run through `AgentBridge::stopPage(id)`, not through `stopWaiting()`. An Ask made meanwhile
takes the agent's waiting; Stop still ends the page run and leaves the Ask alone.

**Files after `agentDone`.** An agent may write or format once more before it exits. When `agentDone` arrives while the
agent's process still runs, the bridge waits for the process to end (at most 5 s, then it stops it) before the files are
read. An agent in a terminal has no process to wait for, and its files are read at once.

**The plan and its object.** The plan's dialog runs its own event loop, and the agent's socket is answered meanwhile, so a
reset or a closed document can end the `BrowserViews` under it. The code keeps a `QPointer` across the dialog and in the
plan's `apply`. After a reset, Confirm still writes the project, and a notice says no server runs for it. A repository that
appears in the folder while the plan shows stops the create ("became a git repository since the preview").

**The plan waits for the reply.** `agentDone` comes through the agent socket, which
waits for its reply. The plan opens on the next turn of the event loop, so the
agent's command does not wait for the person to answer.

**Checks before the plan.** The template's files are compared with the agent's in path order, so a refusing agent is noticed
for every stack. The folder's name is HTML-escaped in `<title>` and `<h1>`. No description, a folder that is a file, and a folder
that has anything in it (hidden files too) are refused in the sheet. An agent that
left the template as it was, or wrote more than 200 files or 8 MB, gets no plan; the
person sees why. `.git`, `node_modules`, `dist` and `.astro` in the staging folder
are ignored, and so are links.

**What "Then runs" means.** The plan lists `npm install` and `npm run dev` (or
"Omastrator's static server, on the folder"), but the runner does not run them: a dev
server never exits. `apply` calls `DevServers::acquire`, and the frame's address
changes when the server answers (one "Change URL" undo step, so the frame is empty
again after one Undo). A server that fails leaves the project in place, and the notice
says where it is.

**The dev server's life.** `BrowserViews` holds one lease per generated frame. It
lets go when the frame goes, when the document closes, on reset, and when the frame
is generated into again (after an Undo). Another address in the frame does not release it.

**Whose page it is.** The dev origin is not registered, at Confirm or later, because a port belongs to no project for long:
a registry entry for `http://127.0.0.1:5173` would make another project that gets that port later look like this one, and
Save, Deploy and Build It would write into the wrong folder. The frame keeps the folder in memory
(`BrowserViews::generatedProject`) while it shows that origin. So it is not "Not your site", the bar shows "dev", Deploy and
Save act on the folder, and Edit Page starts Live with the folder and with `remember` off (`LiveFrames::start(frame, folder,
false)`, `LiveSession::Target::remember`), so Live does not put the address in the registry either. The link is not saved in
the file: a reopened document shows the address with no server behind it.

**Folder suggestions.** The sheet does not list `ProjectRegistry::suggest` results.
Every suggestion is an existing project, and Generate refuses a folder that has
files. The field starts at `~/Projects/<name>`, made from the description
(`PageTemplates::slug`), and follows the description until it is edited.

**Build It from an empty frame.** With no address, Build It opens the same sheet
without the description. A new or empty folder gets the starter project and the plan
first, with no agent. Then Build It runs on it, as it does for any own site. A folder
that already holds a project skips the plan and builds into it as it is. Cancel on
the plan writes nothing.

**Activity.** `AgentBridge::activity()` holds the lines. The Live panel opens with
the run and lists them: ✓ done, … running, · waiting, ✗ failed. The frame shows the
running line over its empty page, and the bar's Build pill reads "Writing with
Claude…" and stops the run. Esc stops it too, after it has cancelled a drag or an armed link, and only for a frame on the
page in front. The lines are: the tokens (when the
document has any), writing the page, the commit, `npm install` (for the stacks that
run it) and the dev server.

**Design system tokens.** When the document has tokens, they go into the template's
token file before the agent starts: `TokenFiles::writeTailwind` for Vite + Tailwind,
`writeCss` for the other two. The prompt tells the agent where they are.
