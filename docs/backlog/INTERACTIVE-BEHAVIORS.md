# Interactive behaviours: "fourth-wall" design for sites and apps (idea, 2026-09-28)

**Status: an idea for the backlog, from the author on 2026-09-28. Not
scheduled.** It depends on Browser View (docs/BROWSER-FRAMES.md), phases 3–4.

## The idea
Let designers build the playful, interactive touches people now see on landing
pages without writing code. The example: a character whose eyes track the
cursor. Those pieces are simple underneath: parts of a drawing react to an
input (cursor, scroll, time, click, idle) through a rule (look at, follow,
tilt, blink, react), within limits (the eyes stay in their sockets). Rive and
Framer sell this; Figma can't do it.

## Not a separate mode
A mode with its own tools would split the app and duplicate selection, layers
and properties. Instead:
- **Behaviours on any layer:** a folded "Behaviours" section in Properties,
  like Figma's Prototype tab. To start:
  - *Look at cursor:* rotates around a pivot, within an angle limit;
  - *Follow cursor:* with a spring or a lag, inside a box;
  - *Parallax:* on the cursor or on scroll;
  - *Blink or loop:* every N seconds, with jitter;
  - *React:* switches between component variants on hover, click or idle.
- **A Rig tool:** set pivots and movement limits by dragging on the canvas
  (pin an eye to its socket). The one genuinely new tool.
- **Preview:** a Play toggle on a frame, and above all live inside a Browser
  View frame, on the user's own site with the Browse tool. Designing the
  character in place on the real page is where Omastrator wins.
- **Ship it:**
  - a small self-contained web component (`<omastrator-scene>`: SVG and a
    few KB of script);
  - or Browser View's "Build it", which hands the behaviours to the agent to
    write into the project's own code.
- **AI in the flow:** "make his eyes follow the cursor and blink when idle"
  becomes visible, editable behaviours, not opaque code.

## Rules
- Respect `prefers-reduced-motion` by default, and never hide or take over the
  real cursor.
- Keyboard and screen-reader users lose nothing.
- Behaviours are stored as data in the `.omai` file (an additive codec key), so
  a file with behaviours still opens in a build without the feature.

## Costs and risks
- The runtime is real JavaScript that someone has to maintain, and it must run
  the same way in the editor preview and in the export.
- Performance: many listeners on a page. Share one pointer and animation-frame
  loop per scene.
- Testing needs headless Chromium (as the Live tests already use).

## A spike, when it's scheduled
After Browser View phase 3: the five behaviours, the Rig tool, Play preview
and the web-component export, on one character. If it feels good, it's a
headline feature: "fourth-wall design, on your live site".
