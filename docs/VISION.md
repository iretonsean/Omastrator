# Why Omastrator exists

Written 2026-09-27 from the author's own words. Every feature decision answers
to this page.

## The thesis

Illustrator is enormously powerful, and that power is worth keeping: the
control, the precision, the granularity. But a designer shouldn't spend their
day fiddling with menus and overcomplicated UI to reach it. Omastrator brings
Illustrator's depth into an experience that feels like Figma and Paper, with AI
at its core.

The software streamlines a designer's workflow **without stripping away what
makes the designer useful**. Automation speeds up the creative process; it
never takes it over. The designer stays the author, has more room to be
creative, and has fun working.

Above all, Omastrator removes the layers of separation between the designer
and the customer they're designing for.

> A client asks for a smiley face. You draw one on a sheet of paper, and
> they're happy with it. The exchange should feel that simple.

## Principles

1. **Nothing between intent and result.** The shortest path from what the
   designer means to what the customer sees wins: direct manipulation first,
   with no detours through dialogs or setup. (The smiley face above is a
   metaphor for how simple the exchange should feel. It is not a
   sketch-recognition feature.)
2. **Power one step away, never in the way.** Every Illustrator-grade control
   exists, but only the essentials are on screen. The rest sits behind a
   disclosure, a context menu, the command palette or a shortcut, and appears
   when the selection calls for it. Deciding what *not* to show is part of
   designing each feature.
3. **AI works in the flow, and the designer decides.** Ask for things where
   you're already looking: on the selection, from a right-click, from Ctrl+K,
   or out loud. Results are previewed and always editable, and the designer
   keeps them or throws them away. AI never produces a flattened result or
   makes a silent change.
4. **Automate the tedium, not the taste.** Alignment, spacing, cleanup,
   variations, exports, handoff and deploys are the machine's job. Choosing
   what's good is the designer's.
5. **Close to the customer.** Design on the real thing where possible (Live
   editing of the actual site or app). Get it in front of the customer in one
   step (share, deploy, preview), and bring their feedback back just as fast.
6. **Fun is a feature.** Speed, delight, personality (see HUMOR.md), and no
   friction that exists only because software is usually like that.

## What that means in practice

- **Contextual over global.** A small task bar next to the selection offers
  the next likely actions, including AI ones. Properties shows what matters for
  this selection, with advanced fields folded away.
- **One command surface.** Ctrl+K reaches every command, setting and AI action
  by name, and plain-language requests go to the agent as a preview.
- **Sensible defaults, no setup screens.** New documents, exports, deploys and
  agent runs work without configuration, and remember what you chose.
- **Invisible machinery.** No terminals, permission prompts or diffs unless you
  ask for them (the review lives behind a button).
- **Keep the depth.** Kerning, tracking, per-corner radius, Pathfinder, Shape
  Builder and precise transforms all stay: fast to reach, never mandatory.

When a feature makes the designer think about the software instead of the
customer, it's wrong until it's simpler.
