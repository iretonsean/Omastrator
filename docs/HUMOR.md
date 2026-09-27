# Humor in Omastrator

Decided with the user on 2026-09-26. The model is the dry, specific humor of
the web design time machine on seanireton.com. The goal is that nobody ever
wants to turn it off.

## Rules

- **Voice:** deadpan delivery about design-world subjects: clients, kerning,
  Comic Sans, "make the logo bigger", Pantone, feedback rounds. No exclamation
  marks, no emoji, no "Oops!".
- **Rare, and never repeated.** Each line shows at most once per install. The
  app records which lines have been used. Actions people repeat all day
  (saving, undo, switching tools, zooming) never get a joke.
- **The useful part comes first.** In errors and alerts, the plain message leads
  and the joke is the second sentence. If you deleted the joke, the message would
  still be complete.
- **Humor never goes in:**
  - data-loss or destructive prompts, such as "Close without saving?"
  - menu items, button labels or command names, which keep Illustrator's names so
    muscle memory and documentation work
  - accessibility names and descriptions
  - anything that adds a click, a delay or a modal
- **Off switch:** Preferences ▸ Personality, on by default. It's for classrooms
  and screen recordings. When it's off, only the plain sentences show.

## Where it goes

| Place | How it shows | Example |
|---|---|---|
| Empty and waiting states | The welcome screen, an empty Layers panel, progress text for export and trace | Tracing… This is the part where you'd bill for "concept exploration". |
| Errors and alerts | The plain sentence, then the joke | Couldn't read this SVG. You sure your client didn't just send a JPEG with the extension changed? |
| Milestones | A toast, once per install | Your first save. The client will want changes. |
| Default names and tips | Tooltips, tips and placeholder titles; "Layer 1" stays plain | Untitled-1 (final_FINAL_v3) |

## Starter lines

These are drafts. Keep a line only if it would still be funny the one time
someone sees it.

**Errors**
- Couldn't read this SVG. You sure your client didn't just send a JPEG with the
  extension changed?
- Couldn't open this file. It's either damaged or it's a Word document with a
  logo pasted in.
- This image is too large to place. It's 40,000 pixels wide. Someone said "high
  res" and meant it.
- The export failed because the disk is full. Maybe it's time to delete
  `logo_final_v7_REAL.ai`.
- Couldn't find the font "Helvetica Neue Condensed Black". It's in a folder on
  the designer's old laptop, as is tradition.

**Empty and waiting states**
- An empty Layers panel: Nothing here yet. The blank artboard is the only brief
  that never changes.
- The welcome screen, no recent files: No recent files. A fresh start, or a very
  organized person.
- A long trace: Tracing… Counting pixels so you don't have to.
- A long export: Exporting… Printing it and scanning it back in would be
  slower. Slightly.

**Milestones** (each once, ever)
- The first save: Saved. The client will want changes.
- The 100th undo in one session: 100 undos. Your first instinct was right.
- 1,000 anchor points in one path: One path, 1,000 anchor points. Object ▸ Path
  ▸ Simplify is right there.
- First use of Comic Sans or Papyrus: Bold choice. It's legal, technically.
- The logo is scaled up 3 times in a row: The client will be thrilled.

**Tips**
- Hold Shift while you drag to keep proportions. Or don't, and give the client
  the stretched look they secretly asked for.
- Alt-drag duplicates. Each duplicate is another direction for the client to
  ignore.

## Roast My Design

The one place the humor goes loud. It's a button at the bottom of the tool rail,
and it's the only playful button label in the app.

1. **The roast (savage).** Full comedy-roast energy, aimed only at the design and
   never at the person: layout, type, colour, alignment, the fourth drop shadow.
2. **Then sincere feedback,** immediately after and clearly separated: specific,
   actionable fixes in order of impact, pointing at the actual objects.
3. **Then one click to generate.** "Make variations from this feedback" goes
   straight into the generation flow in docs/AI-ROADMAP.md: choose how many
   variations, preview, accept.

It works on the selection, or on the whole artboard when nothing is selected. The
agent is sent a rendered PNG and the document JSON, as in AI-ROADMAP.md. It runs
on Omarchy's default agent like every other AI feature. The Personality switch
doesn't hide it, because using it is always a choice.
