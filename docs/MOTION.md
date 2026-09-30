# Motion in Browser View (design, 2026-09-30; phases A to D built here; E and F are on their own branches)

The live page in a Browser View is the artboard for motion. The designer asks
the agent for an animation, sees it as tracks on a timeline, tunes it with
designer controls, and saves it to code. The code stays the only source of
truth: the agent writes plain CSS (or the project's own stack) into the
project, and Omastrator reads that code back and edits it.

This builds on Live inside the frame (LIVE-IN-FRAME.md): Edit Page, the element
bar, Live's undo, write-back, Save, Deploy and Build It. Phases A (the read-only timeline), B (inspector edits, write-back and motion tokens) C (Animate through the agent, and reduced motion) and D (groups) are built; "Decided while building" at the end says how. The rest is not.

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

## Decided while building

- **Phase A (the read-only timeline, `feat/motion-a`).**
  - **The page side is `src/Live/motion.js`,** not a part of `overlay.js`: a second script that runs after the overlay and
    adds `__oma.motion`. CMake compiles both into `OverlayScript.h`, and `LiveSession::overlayScript()` joins them, so the
    frame host and the window get it in one injection. `list()` returns the page's animations one by one (kind, name,
    selector, parent, keyframes, timing, the scroll range for a scroll-driven one) and does not make rows. The rows are made in
    C++ by `Motion::parse`, which needs no browser to test.
  - **Row merging.** Animations merge into one row when they are the same kind and name, with the same parent and clock:
    `nl-rise` on five siblings is "h1 .word × 5". A transition row is one per element, whatever properties it moves, so a
    hover row keeps its id when its state is held. A row's label is the tag with an id or the first class; several
    elements read "parent .class × n" or "tag × n". The id is `kind|name|parent|timeline`, which stays the same across
    refreshes.
  - **What `hold()` does.** It pauses every animation on the document timeline and keeps pausing the ones that start later
    (the `transitionrun` and `animationstart` events, and a check once per frame with a 250 ms timer behind it, since a
    hidden page draws no frames). It does not pause scroll-driven animations, which scrolling moves. Each held animation
    remembers its offset: where its own time zero sits on the timeline (starts within 34 ms of each other are one start). An
    animation that joins late starts at offset 0 and at the playhead, but never past its own end, because a transition that
    has ended is gone and could not be scrubbed back. `release()` plays the rest on from where they are; an animation that
    had ended before the hold ends again, and is not played from its start.
  - **Scroll-driven rows.** The bar runs between two scroll positions in px, from the timeline's own `startOffset` and
    `endOffset` where the page gives them, else from the subject's box. `animation-range` names (entry, exit, contain, cover,
    the crossings) and percentages are applied. Seeking such a row is `window.scrollTo`, and the page's scroll (a wheel over
    the frame) moves the scroll playhead. Chromium keeps a scroll position on whole device pixels, so the page is within a pixel
    of the ruler.
  - **Hover rows before there is a pointer.** A transition that only a `:hover`, `:focus` or `:active` rule starts is not in
    `getAnimations()`, so `list()` scans the style sheets for such rules (only where the state is on the last compound of
    the selector), finds the elements that have a transition for a property the rule sets, and lists them as rows that are
    not held yet (dashed bars, "hover to preview"). Picking such a row calls `LiveSession::motionForce`, which forces the state
    with `CSS.forcePseudoState`; the page then starts the real transition, which is held, and the row is real with the
    same id. Letting the state go (another row, closing the timeline, Stop Live) drops the transition back that the page
    starts, so the page is as it was. The protocol keeps a forced state per node and starts its nodes over at each
    `DOM.getDocument`, so the session asks for the document once and lets a state go on the node it put it on.
  - **The GSAP row** is read from `gsap.globalTimeline` (`getChildren`, `startTime`, `totalDuration`): one row, held with
    `pause()`, seeked with `time()`, let go with `resume()`. The tests use a stand-in with the same calls, because no GSAP is
    shipped in the repository.
  - **Where the timeline is.** `MotionTimeline` is a widget in `ContentView`'s canvas column, under the canvas, hidden until
    Window ▸ Timeline. Opening it enters Edit Page on the frame (the Browser View in Edit Page, else the selected one, else the
    only one on the page), starts Live there, and holds the page once Live runs. It follows Edit Page to another frame, and
    Edit Page ending, Stop Live, reset and the × close it. It does not open by itself yet: Animate (phase C) opens it, and
    "when the selected frame's page has motion" would need Live on every selected frame, which starts a dev server, so it is
    not done. The × closes the timeline and leaves Edit Page on.
  - **Scrubbing.** One seek is in flight: `LiveFrames::run` answers, then the frame's next picture (`BrowserViews::pictureArrived`)
    or 120 ms lets the next seek go, and a newer position replaces the one waiting. A frame with its timeline open streams
    every picture (`BrowserViews::setScrubbed`). Play advances the playhead itself, 15 ms apart, asking for the time since Play
    began, so it runs at the picture's pace; Loop starts again at the end; Replay is Play from 0. With only scroll-driven rows,
    Play scrolls the page down its range over two seconds.
  - **The inspector** (`MotionInspector`, Window ▸ Motion, opened with the timeline) shows the selected row: its name, what
    starts it, the easing (named where it is a preset), duration and stagger (the even gap between the bars' starts), the
    motion tokens declared in the row's marked block, the reduced-motion box (checked when the block has the rule, disabled
    until phase B) and the keyframes. A script's row says it is not shown here. Nothing in it is a field yet.
  - **The Code tab** shows the marked blocks of the project that name the selected row's `@keyframes` (all when none is
    selected). `MotionCode::blocks` reads style and markup files under the project, skipping `node_modules`, `.git` and build
    output, at most 600 files of 1 MB. A click on a line opens its file with `xdg-open` (`OMASTRATOR_XDG_OPEN` replaces it in
    tests). A page that isn't one of the user's sites says its code isn't here.
  - **Not in phase A:** `__oma.motion.trigger` (a Click trigger's preview, phase C), Record MP4 and Save to code in the header
    (phases C and F), pending edits marked in the Code tab (phase B).
  - **Tests:** `MotionModelTests` (rows, labels, ids, stagger, scroll rows, GSAP, the marked blocks, no browser),
    `MotionTimelineTests` (Chromium: the list of each kind, hold, a later animation joining, seek, scroll seek, release, hover
    forcing and letting go, a reload holding again, GSAP), `MotionTimelineUiTests` (Chromium: rows per parent, a row click
    selects, the ruler scrubs and drags, a fast scrub sends few seeks, the picture changes after a seek, Play, Loop, Replay,
    scroll, a hover row, Esc, the × button, Stop Live, a page with no motion, the Code tab, the inspector) and one case in
    `MenusTests`. The fixture is `tests/Live/fixtures/motion/` (`index.html`, `style.css` with a marked block, `gsap.html`).

- **Phase B (inspector edits, write-back and motion tokens, `feat/motion-b`).**
  - **Which field edits what.** The Motion inspector's fields are the row's tokens when the code holds each value in one, and a
    change for the agent when it doesn't. `MotionCode::bindings` reads the rule that runs the row's animation (`animation` or
    `animation-name` naming the `@keyframes`) and takes `var(--duration-*)`, `var(--ease-*)` and `var(--stagger-*)` from its
    animation declarations: **Duration** edits `--duration-x`, **Easing** (a preset menu, the curve editor and its text) edits
    `--ease-x`, **Stagger** edits `--stagger-x` and is disabled, with a tooltip, when the stagger isn't a token. A row with no
    duration token records `animation-duration` on each of its elements (`LiveSession::motionSetTiming`, `effect.updateTiming` on
    the page), which `WriteBack::plan` leaves for the agent; the same for delay and easing. The list of tokens in the inspector is
    the block's own (`MotionCode::tokens`), each editable, showing a pending edit as its new value.
  - **Preview, then one edit.** A scrub or a curve drag calls `motionPreviewProperty` (an inline custom property on `:root`,
    remembered so that `endPreview` takes it off first) on each step and nothing is recorded; letting go calls
    `motionSetProperty`, which is one `LiveEdit` (selector `:root`, property `--duration-x`) with an undo step. A typed value (Enter)
    is a step with nothing to preview. The page's animations follow at once, because a CSS animation's timing is computed from the
    property, and the timeline reads the new durations back from the page.
  - **Keyframes.** A keyframe value typed in the inspector calls `effect.setKeyframes` on every running animation of that
    `@keyframes` name and records an edit with selector `@keyframes nl-rise` and property `from opacity` (the keyframe as the code
    names it, then the property). Undo and redo restore the keyframes the overlay saved (`__oma.restore` now takes `{motion: state}`
    and `{codeOnly}`, for the edits that have nothing to put back on the page).
  - **`WriteBack::plan` writes three more things, each only when exactly one place matches** (`MotionWrite`, `CssRules`, a small
    reader of CSS blocks that knows comments, strings, nesting and `;`):
    1. a custom property inside the one rule whose selector text is the edit's selector (`#huila { --i: 2; }`): its value is
       replaced, or the declaration is added to the rule (inline for a one-line rule, on a line of its own at the same indent for a
       rule of lines); a rule that occurs twice (also inside `@media`) is left for the agent;
    2. a value inside one `@keyframes <name>` block: the name occurs once in the project, the frame (`from`, `to`, `40%`, a list such
       as `0%, 100%`, with `from` = `0%` and `to` = `100%`) matches one rule of the block, and the property is declared once in it. A
       block that never closes is not read at all;
    3. the reduced-motion rule of a marked block, taken out (its text is the edit's `before`, and it must be the text that is there) or put
       back before the end marker (its text is `after`). Off and on again in one session nets to `before == after` and writes nothing.
    A custom-property edit for one element is never written into another element's rule: `--i` declared once on `#guji` isn't the
    declaration for `#huila`. Motion tokens are on `:root` (or `@theme`), and the old rule for a property declared once still writes them.
  - **Reduced motion in the inspector.** The box shows what the code will have once the pending edits are written: the file's rule
    XOR the pending toggle. Off records the rule's text; on puts the same text back, or the rule the block's own animations need
    (`MotionCode::defaultReducedRule`: `@media (prefers-reduced-motion: reduce) { .word, .lede { animation: none; } }`) when it
    never had one. The code is read again when the pending edits change (a Save writes them), so the box follows the file. Preview
    reduced and the warning for motion with no rule are phase C's.
  - **Motion tokens in the model.** `TokenKind` gains `duration` (ms; a stagger is a duration named in a `stagger` group) and
    `easing` (`TokenValue::text`: `cubic-bezier(…)`, a keyword, or `linear(…)`, shown as "Custom"). The Design System panel lists
    them, can add and edit them, and Sync writes them with the rest. Files: W3C `$type: "duration"` (`{"value": 480, "unit": "ms"}`,
    or the text `"480ms"` in a file that already writes text) and `cubicBezier` (four numbers; a keyword or `linear()` is text with
    `$type: "string"`, found again by its path `ease/*`); Tailwind v4 and CSS `--duration-*`, `--stagger-*` and `--ease-*`, in the
    unit the file already uses (a file that writes `0.6s` keeps writing seconds). The W3C reader no longer skips durations.
  - **The page's tokens.** `Live::TokenSet` gains `duration`, `easing` and `stagger` groups from the scan (names and values, since the
    overlay reports a time as text), and snaps `animation-duration`, `transition-duration`, `-delay` and `-timing-function`: a time
    to the nearest duration token, an easing to a token only when the page has exactly that value.
  - **The Tailwind v4 check, done against 4.3.3 (2026-09-29).** With `--duration-reveal`, `--stagger-words` and `--ease-reveal` in
    `@theme` and a rule using them through `var()`, the built CSS declares all three under `:root`; `--ease-reveal` also makes an
    `ease-reveal` utility, and `--duration-*` makes none. A token nothing references (`--duration-unused`) is not emitted unless
    the theme is `@theme static`, which is right for motion: the block that uses a token names it.
  - **Not in phase B:** the Starts control (a trigger), which is phase C; editing a group's order and per-element delay (phase D).
  - **Tests:** `WriteBackTests` (the three rules and what they refuse), `DesignSourcesTests` (durations and curves through W3C,
    Tailwind v4 and CSS, keeping unknown keys and the file's unit), `LiveUnitTests` (the page's motion tokens and snapping),
    `MotionModelTests` (bindings, the reduced rule) and `MotionEditTests` (Chromium: the fields are the tokens; a scrub shows on
    the page and records one edit; the curve and a keyframe undo and redo; Save writes the token, the curve, the keyframe and the
    scoped `--i` into a temporary git repository and nothing else; reduced motion off, on, off; a duration the code holds in no
    token goes to the agent). The fixture `tests/Live/fixtures/motion/cards.html` and `cards.css` is the design's group block.

- **Phase C (Animate through the agent, and reduced motion, `feat/motion-c`).**
  - **The ask.** Animate is a button in the element bar after Ask…, on the user's own sites only; with several elements picked it reads
    Animate together (the signature gains a `g`, so the bar refills when the count crosses one). It opens `AnimateSheet` under the bar
    (a field, the four chips, "Your default agent: Claude", ⋯ with the reduced-motion switch, Cancel and Generate, and the line about a
    branch and the preview). Return writes and Shift+Return is a new line; Esc closes the sheet first, then leaves Edit Page. While the
    agent writes, Generate says "Writing…", Cancel says Stop, and the sheet lists three steps that are Omastrator's own (asked, writing,
    checking and previewing), since the agent reports none. `BrowserViews::animate` checks what it can at once (an agent, a picked element,
    the user's own site, a busy agent) and returns why not; it then reads the page's motion fresh (`motionRefresh`) and asks, so the
    agent starts a moment later and a launch failure arrives as a notice.
  - **The prompt** is `MotionPrompt::animate`: the request and worktree, the selection as `info()` reports it, the page's tokens (the
    motion ones among them), `MotionStack::detect`'s stack line ("Vite + Tailwind v4", "Astro", "Plain HTML and CSS"), the style file and
    the file the tokens go in, the motion libraries in `package.json`, the motion already on the picked elements, every `@keyframes` name in
    use, the frame's picture as a PNG, how wide the page is drawn and the site's breakpoints, and the output contract (plain CSS in the style
    file, tokens as `--duration-*`, `--ease-*`, `--stagger-*` in `@theme` or `:root`, a new keyframes name starting with the site's short name,
    per-element timing through `--i` and `--delay-extra`, the markers, the reduced-motion rule unless the run turned it off, no commit or
    server). It ends with `agentDone`.
  - **The contract check** (`MotionContract::check`, after `agentDone`): the blocks that differ from the project's are the agent's; each
    must be closed, every `var(--duration-*|--ease-*|--stagger-*)` in them must be declared somewhere in the worktree, each `@keyframes` name
    must be written once, and a block that moves something must have a `prefers-reduced-motion` rule when one was asked for. A result that
    breaks it is still previewed, and the inspector says "Some of this motion isn't tunable here. Ask to fix it, or edit it in code."
  - **The preview.** `AgentBridge::liveAnimate` starts the agent in a worktree like any Live run, and `liveAgentDone` for an Animate run
    collects the diff and does not apply it. The worktree is served with `DevServers::acquire(worktree)` for every stack: its own dev script,
    an `omastrator.json` command, or, for a plain page, the static server `DevCommand::detect` already falls back to. A project with
    `node_modules` has it hard-linked into the worktree first (`cp -al`), so there is no install and no network. When the server answers,
    `BrowserViews` sends the frame's tab there with `useDevServer`, so the document keeps its own address; the bar's pill says "preview"
    and its tooltip names the branch; the timeline opens on the frame and plays the new page from 0 (`openAndPlay`: it waits for the list
    of a page on another origin than the old one); and a notice says "Claude wrote the motion into cards.css. Preview only until you
    save." The session is told the preview's origin first (`LiveSession::setPreviewOrigin`), so that page is still the project's page:
    what is tuned there is pending edits of the project, not a mock-up's.
  - **Save to code** (the timeline's header, and the API) collects the agent's change again against the project as it is now (what the
    designer changed meanwhile is merged around it), writes it, records one Review named "Animate: article#guji", lets the preview go,
    then writes the pending edits on top through the existing write-back and commits everything through the existing Save (which pushes
    where a Save pushes). **Discard** (the header, the inspector, leaving Edit Page with Esc, Stop Live, reset, closing the document)
    releases the server, removes the worktree and its branch, and drops the edits made since the preview began; nothing was written.
    Edits made before Animate are still pending and are saved on top, but they aren't shown on the preview page (its origin is not the
    one they were made on).
  - **One preview per project.** A second Animate while one is open asks "Keep or discard the motion you're previewing first?" with
    Keep, Discard and Cancel (tests answer it with `BrowserViews::setPreviewChooser`); Discard starts the new ask, the others start
    nothing. **Stop** during "Writing…" is `stopWaiting`: the agent is cancelled, its worktree removed once it has stopped, and the
    Animate is forgotten; a run that ends without answering is forgotten the same way.
  - **Preview reduced** is a switch at the top of the inspector, on while the timeline is open: `Emulation.setEmulatedMedia` with
    `prefers-reduced-motion: reduce`, so the page's own rules apply and the rows show what is left ("No motion when reduced." when nothing
    is). Letting go of the page (closing the timeline, Stop Live) puts the emulation back. The **warning** ("This motion plays for people who
    asked for less motion.", with Ask…) shows for a selected row when the page has no `prefers-reduced-motion` rule at all (the overlay scans
    the style sheets); Ask… hands the agent that one job through `liveAsk`.
  - **Starts** is a segmented control in the inspector (Load, Scroll, Hover, Click) for rows made by CSS animations. Picking one is an edit
    for the agent on each element (property `motion-trigger`, from and to), described to it in words ("Change #guji (the animation
    nl-cascade) to start as it scrolls into view (it starts when the page loads now)"). Scroll is also shown on the page, best effort: the
    running animations move onto a `ViewTimeline` of each element (`setStarts`), and Load moves them back; undo puts them back on the clock.
    Hover and Click have nothing to show until the agent writes them. A row keeps its id when it moves between time and scrolling, so it stays
    picked. `__oma.motion.trigger` (a Click trigger run from the code's class) is not built.
  - **Tests:** `MotionAnimateUnitTests` (the stack, the prompt's contents, the contract), `AnimateTests` (Chromium, fake agent: the sheet,
    the prompt with selection, tokens, stack and existing motion, a result that is a preview with the project unchanged and the document's
    address kept, the pill, the notice and the timeline; Save to code makes one Review and one commit with the tuning on top; Discard leaves
    git clean, removes the worktree and drops the tuning; Stop; a second Animate asks; a broken contract still previews with its line; the
    element bar's button opens the sheet and Generate starts the agent) and `ReducedMotionTests` (Chromium: Preview reduced leaves no
    running animation for the fixture's block and comes back; the warning shows with no rule and not with one; Starts is an edit and Scroll
    shows and undoes).

- **Phase D (groups and multi-selection, `feat/motion-d`).**
  - **A group is a row of several elements.** Animate together (phase C) sends the picked elements in one prompt, which asks for one rule
    for the shared class and one index per element (`#guji { --i: 1; }`). The page then reports each element's `--i` and `--delay-extra`
    (computed custom properties), and the model keeps them on each bar (`Bar::index`, `Bar::extra`, `Bar::label`).
  - **Numbers on the page.** With two or more elements picked, the canvas draws a numbered badge in each one's corner in the order they
    were picked (`EditorCanvas::editPageBadges`, which the paint and the tests both use); one pick has none.
  - **The row opens.** A row of several elements has a triangle (`▸`/`▾`); open, it shows one row per element, labelled like `article#guji`,
    each with its own bar. Picking the group row picks all its elements on the page, picking an element's row picks that one; the row's id does
    not change while it is open. The dock grows to fit, up to about eight rows, and scrolls past that.
  - **The inspector with a group picked** is "Group · 3 bean cards" (the class read as words, in the plural), with **Order** (As picked,
    Left → right, Centre out, Shuffle), **Effect** (Rise, Grow, Flip) and the group's Duration and Stagger tokens. **Order** needs the
    row's `animation-delay` to read `var(--i)`; when it doesn't the buttons are off, with a tooltip. The indices come from
    `Motion::order`: as picked uses the order the elements were picked in (the picked ones that are in the row first, then the rest in
    the page's order), left to right sorts by each box's middle, centre out by the distance from the middle of them all (ties go left),
    and shuffle is a Fisher–Yates over the standard Mersenne twister, fixed by the elements and by how often it was pressed (so a
    reload gives the same order, and pressing again gives another). The boxes are read from the page when the order is asked for. Each
    element's `--i` is set inline for the preview and recorded as one edit, all three as one undo step; write-back puts each in its own rule.
  - **Effect** replaces the first keyframe's declarations (Rise: `opacity: 0; translate: 0 44px`, Grow: `opacity: 0; scale: 0.85`,
    Flip: `opacity: 0; rotate: y 70deg`): one edit with selector `@keyframes nl-cascade` and property `from *`, previewed with
    `setKeyframes` on the running animations and written by `MotionWrite::keyframeBody`, which keeps the frame's one-line or many-line shape.
  - **One element's own timing.** Picking an element's row shows "Extra delay" (ms) and "Use the group's timing". The field writes
    `--delay-extra` in that element's rule (added when the rule lacks it), and giving it back is an edit with an empty value: the preview
    takes the property off the element, and write-back takes the declaration out of the rule (and leaves the rest of it). Only that
    element's start moves. Off and on in one session nets to nothing to write.
  - **Not built:** a group made of elements that share no rule (each element's own delay and the order then go to the agent, as
    write-back finds no rule to write `--i` in); the sheet's Animate together has no separate group options.
  - **Tests:** `MotionGroupTests` (Chromium: the numbers in pick order; the row opens and each element can be picked; each order writes
    the expected indices and Save writes each in its own rule; a group change is one undo step; an extra delay moves one element's start
    and giving it back restores it; an effect replaces the first keyframe, undoes and is written), `MotionModelTests` (the orders from
    boxes, shuffle by seed, the group's name) and `WriteBackTests`.
- **Phase F (Record, `feat/motion-f`).**
  - **A screenshot, not the screencast.** Each step puts the page at its time and takes `Page.captureScreenshot` of the
    frame (`BrowserViews::capture`). The screencast sends a picture only when the page paints, so a step that paints what the
    last one did (a still stretch of the motion, or a seek that changes no pixel) would wait for ever. The capture waits for two
    animation frames, or a quarter of a second for a page nobody is looking at, so the seek has been painted. It asks the tab
    for the frame's size on screen at most 2560 px on the longer side; a screenshot's scale multiplies the tab's own density,
    so the density is divided out. A frame that isn't live (paused, or its tab opening again) gets three seconds to come back.
  - **The steps.** `MotionRecorder` makes `round(seconds × fps)` pictures at times 0, 1/fps, 2/fps…, so 2.0 s at 30 fps is
    60. The last picture is one step before the end, not at it. A motion with only scroll-driven rows records a scroll of its
    range over two seconds, as Play does; a page with both records the time axis. Seeking is the timeline's own
    (`motionSeek`, `motionSeekScroll`), so a GSAP timeline records as it scrubs and script-driven motion with no clock to seek is
    not moved.
  - **ffmpeg.** `FrameRecorder` runs `ffmpeg -y -f image2pipe -framerate 30 -c:v mjpeg -i - -vf
    scale=trunc(iw/2)*2:trunc(ih/2)*2:out_range=tv -c:v libx264 -pix_fmt yuv420p -movflags +faststart <file>`, with
    `-loglevel error`, so the last line on stderr is the reason. Three things differ from the sketch in section 8: `-c:v mjpeg`
    (ffmpeg cannot always tell the codec from a pipe), the even-sides scale (yuv420p refuses an odd size, so an odd picture
    loses a row or a column) and `out_range=tv` (JPEG's full range would be tagged `yuvj420p`, which players show with the
    wrong colours). A GIF writes the pictures beside the file as numbered JPEGs, in a hidden folder called
    `.omastrator-frames-XXXXXX` (removed at the end; a crash leaves it, and it is there rather than in `/tmp` to spare a
    RAM-backed disk), and runs two passes, `palettegen` then `paletteuse`, each with up to 120 s. The pictures go to ffmpeg as they arrive; a run that is faster than the
    encoder waits for 4 MB to drain.
  - **Failure and Stop.** A failure removes the partial file and says "Couldn't record: <ffmpeg's last line>". Stop, and Esc,
    finish the step in flight and end the file where it is; the file is kept. A step whose seek or picture never answers
    does not hold Stop: two seconds after Stop the file ends with the pictures taken so far. Closing the timeline, Edit Page ending and
    Stop Live abort the recording and remove the file, since it was never whole. With no picture taken nothing is kept.
  - **The header.** Record MP4 is the button; while it runs it reads Stop, the line beside it reads "Recording… 0.40 s / 1.20 s",
    and Play, Replay, Loop, the ruler and the row clicks do nothing. After it, the line reads "Recorded 1.2 s · hero.mp4"
    and a click opens the folder (`xdg-open`, or `OMASTRATOR_XDG_OPEN`; the file's folder, or the frames folder itself). The ⋯
    has Record GIF…, Record MP4 at 60 fps… and Save Frames as PNG…. The playhead goes back to where it was. The save dialog
    starts in the last folder used (else `~/Videos`, else home) with "<site>-motion.mp4", and adds the ending if it is left off.
    The dialog asks about replacing the name as it was typed, so a name that gains its ending and is a file that exists is
    asked about again (Replace the file?); ffmpeg runs with `-y` and would not ask.
  - **Esc** goes to a hook on the canvas first (`EditorCanvas::setEscapeHook`), set only while a recording runs. The first Esc
    stops the recording; the second leaves Edit Page and closes the timeline.
  - **Without ffmpeg** (`OMASTRATOR_FFMPEG`, else `ffmpeg` on PATH) Record MP4 is disabled with "Recording needs ffmpeg. Install it
    with sudo pacman -S ffmpeg."; the GIF and 60 fps items are disabled with the same words; Save Frames as PNG… works. It
    writes `frame-0001.png`… into a new folder, and refuses a folder that has files in it.
  - **Tests:** `FrameRecorderTests` (the fake ffmpeg's arguments and counts, a failure at once and at the end, a GIF's two
    passes, PNG frames, and the real ffmpeg checked with `ffprobe`: h264, yuv420p, even sides, the frame count),
    `MotionRecordTests` (Chromium: 60 pictures for 2.0 s at 30 fps, each with the box a little further along, Stop, a failing
    ffmpeg, a failing seek, PNG frames, an abort, the real ffmpeg) and `MotionRecordTimelineTests` (Chromium: the header's
    controls, no ffmpeg, the save dialog, 60 fps and GIF from the ⋯, Cancel, Stop, Esc, closing, a failing ffmpeg).

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

## 13. Integration onto the window layout, and the review fixes (2026-09-30)

The phases A to F were merged once onto `main` (the window layout, the Browser View switch, the desktop island's removal), then
one round of fixes followed from the reviews of A to C, D, and E and F. This section lists what the merge decided and what the
fixes changed; the sections above say what was designed.

**What the merge decided**

- **An empty frame is a Browser View that is on, with no address** (`showsPage()`). A plain frame, and a Browser View whose switch
  is off, offer nothing to generate into; Generate says "Turn the frame's Browser View on first." The timeline opens only on a
  frame that shows a page.
- **A generated page's dev server follows the switch.** Its lease is `BrowserViews`' own (not a Live session's), so the switch
  calls `DevServers::setPaused(lease, !on)`: off freezes it, on wakes it, as for a server Live holds. Animate's preview servers
  are the bridge's leases and are not frozen by the switch.
- **The timeline stays under the canvas**, in the canvas column. The window layout has no bottom panel to dock in.
- **Esc order:** the recording's hook, Edit Page, an armed link, a drag, the pen, a held width, `stopGenerating`, isolation,
  picked nodes, the selection.

**What the fixes changed**

- **Answers to a gone owner (A1, B2, D).** `LiveFrames::run` takes a context object, and its `done` is not called once that object
  has gone; every `done` in the timeline that captures `this` passes it. A tab switch deletes the timeline while a seek is in
  flight, and the answer used to read freed memory. The timeline's destructor gives the frame back its own streaming rate, the
  visible inspector follows the front document's timeline (and deletes itself when its timeline goes), and the registry entry is
  removed only by the timeline that owns it.
- **Release (A2).** A page's own paused animation stays paused when the hold ends. **Limit:** after `pause()` and `play()` from
  script, a CSS animation no longer follows `animation-play-state` from a rule such as `:hover { animation-play-state: paused }`
  until the page reloads. The hold is script-driven, so this is the price of holding a CSS animation; a page that relies on
  that rule while the timeline is open will not see it work.
- **The ruler's zero** is the earliest start among animations that are still running, not the start of a load animation kept by
  `fill: forwards`.
- **Leaving a frame** returns at once when nothing is held, forced or emulated, and its two evaluates use the short timeout, so a
  hung renderer no longer blocks every frame's pool thread. A hold that begins again after a reload starts with no forced state
  in the timeline.
- **The Code tab** walks the project without entering `node_modules`, `.git` or build output, and reads a file again only when its
  size or time changed.
- **Write-back (B, D).** `CssRules` blanks comments (`/* */`, and `//` for Sass and Less, but not inside a string or a `url()`),
  and `$` is the end of the rule's body, so a value of several lines is one value (a two-line shadow list is replaced whole, and
  taken out whole when given back). A markup file is scanned only inside its `<style>` bodies. A value with `;`, `{` or `}` is
  not written (an easing is one value: `TokenFiles::isEasing` no longer accepts a second declaration), and a frame rule that
  lists several offsets (`0%, 100% { … }`) is left for the agent, since the page's preview changes one. A whole frame is replaced
  by `keyframeBody` only when its last declaration is not on the brace's line, and it keeps the frame's own
  `animation-timing-function`.
- **One element's value never goes into the project's one declaration (D1).** When no rule names the element, `--i` and
  `--delay-extra` go to the agent. Before, the one place the project declared `--i` was written, which could be a template
  loop (`style="--i: {i}"`) or a `:root` default that every element reads.
- **Animate (C).** Save to code writes only the files the agent wrote, as the preview listed them. When the agent has said it is
  done but its process still runs, the preview waits for the process to end (at most as long as a page run does), so what it
  writes after saying so is in it. The preview's server never installs packages (`DevServers::acquire(…, allowInstall = false)`):
  a project without them fails to preview and says so, instead of rewriting its lockfile in the copy. The `node_modules` hard
  links are made beside the window, not in it. **Deviation from the review:** the copy is still `cp -al` (hard links), not a
  symlink: a symlinked `node_modules` resolves outside the worktree, where Vite's file serving refuses it. Save checks for a
  running deploy before it writes. Closing a document during a preview no longer runs the discard through the session that
  is going. Page content in the prompt (the selection's markup and text, the tokens, the existing motion, the names in use) is
  fenced as data. A new keyframe name starts with the site's short name, or the project folder's for an address that names
  none (an IP address, localhost), and always with a letter. Stop before the agent was launched ends the ask, and a launch that
  failed resets the sheet.
- **Groups (D).** The Effect list shows Custom unless the first keyframe is exactly what the preset writes, and picking the item
  already shown changes nothing. "Use the group's timing" changes the page even when the rule holds a delay. Extra delay is
  offered only for a css-animation whose delay reads `var(--delay-extra`. Order with an element missing sets none, and a group
  numbered by `--i` needs no token. The inspector rebuilds after a drag, a curve or typing, with one retry timer.
- **E and F fixes** (in their own sections above) came in with their branches.

**Left, from the reviews:** Animate's preview servers do not follow the Browser View switch; a page that reloads during a
recording continues on the new page; `emptyFrames()` is computed more than once per paint; centre out rounds distances to 4 px,
so two mirrored elements 1 px apart can land in different steps; a Shuffle after a tab switch can repeat the order the page has;
the picked bar is an index, not a selector; `groupName` builds plurals with "s" and is not translated; GSAP's seconds share an
axis with milliseconds; and the smaller items of the reviews' "Later" lists that are not named here.

