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

The one place the humor goes loud, updated 2026-09-26 after the first live roasts read as too polite and too long. It's a button at the bottom of the tool rail,
and it's the only playful button label in the app.

The panel has three pages, which the user steps through with Back and Next or
the arrow keys:

1. **The roast.** Two to four one-line burns, 60 words at most. It should hit
   like a Comedy Central or Netflix roast (the Roast of Tom Brady, Jeff Ross,
   Nikki Glaser, Greg Giraldo, Don Rickles), not like a critique with jokes in
   it. It's the meanest set of the night (Hinchcliffe, Jeselnik, Giraldo, Ross
   with the gloves off), with no warmth on this page. A polite roast is a
   failed roast.
   - *How it's built:* the roast mechanics are the humiliating comparison,
     fake praise then the knife, consequences instead of flaws, a tiny fact
     blown up, and a short setup with a hard turn.
   - *How hard it hits:* at least as hard as "Violet-to-cyan on near-black.
     Congratulations, you designed every crypto startup that rugged its users in
     2022." A line that only describes the flaw fails. The agent instructions
     carry a few benchmark lines like that one, which the agent must never reuse.
   - *Dark humour:* morbid turns (death, funerals, obituaries, crime scenes,
     hospice, last rites) aimed at the work and the career. Never suicide,
     self-harm, real tragedies or the person's illness.
   - *Disbelief:* one line may open like a friend seeing the file ("Holy shit
     dude, are you fucking drunk? Wtf is this?"), followed at once by the worst
     specifics.
   - *Swearing:* welcome wherever it makes a line hit harder: fuck and its
     variants, wtf, holy shit, shit, ass, damn, hell. Up to two fucks and four
     swears per roast, with at least one clean line.
   - *Target:* the design and the taste, skill, habits, career and ambitions it
     reveals, in second person, drawing on the people who use this app:
     AI-leaning designers, Omarchy and Linux ricers, Figma and UI/UX people,
     the habits of each tool, and the profession. Always about what the user
     pointed at.
   - *Never:* slurs, sexual content, bodies or looks, race, religion, gender,
     sexuality, disability, age, family tragedy, self-harm, or who someone is.
2. **The fixes.** Exactly three, highest impact first, each a title of five
   words and one sentence with the concrete value to use. Clicking one selects
   the objects it's about.
3. **What next.** The brief, then "Make variations from this feedback", which
   goes straight into the generation flow in docs/AI-ROADMAP.md.

The app enforces the lengths: `show_roast` sends back a roast over about 70
words, or more than four fixes, for the agent to shorten.

It works on the selection, or on the whole artboard when nothing is selected. The
agent is sent a rendered PNG and the document JSON, as in AI-ROADMAP.md. It runs
on Omarchy's default agent like every other AI feature. The Personality switch
doesn't hide it, because using it is always a choice.
