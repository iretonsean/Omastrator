# Keyboard shortcuts: Figma's, next to Illustrator's

Omastrator keeps Illustrator's keys and adds Figma's wherever they don't clash
(docs/VISION.md). Every key below is in the Keyboard Shortcuts sheet (Help ▸
Keyboard Shortcuts…, Ctrl+Shift+?) unless it is marked *fixed*, shows next to
its menu entry, shows in the tooltip of its button, and shows in the Ctrl+K
palette.

The Figma column comes from Figma's in-app shortcut panel as third-party cheat
sheets copy it (Figma's help pages don't list the keys). It was not checked
against a running Figma, so a key that is wrong here is a doc bug: fix the
table, not the app.

## Why Shift+A didn't work

It was a *canvas* key: handled by the canvas widget itself, so it only fired
while the canvas held keyboard focus. Clicking a row in Layers or any button in
a panel moves focus there, and from then on the key did nothing. It also had no
key beside Object ▸ Add Auto Layout, so nothing showed it existed.

The fix has two parts:

1. **Commands with a key are menu entries.** Shift+A, Alt+Shift+A, Shift+H and
   Shift+V now belong to their menu items. A menu shortcut works whatever holds
   focus, shows in the menu, the task bar's tooltip and Ctrl+K, and can be
   remapped. Text fields (and open type on the canvas) answer a bare Shift
   letter before a menu does, so a capital A still types.
2. **Tool keys work from panels too.** When focus is on a button, a slider or a
   layer row (anything that isn't a text field, a list that searches by letter,
   a spin box or a combo box), letter keys, digits and X/D go to the canvas as
   if it held focus. Arrows and Delete also go through from a plain button. A
   text field keeps every key it types. See `ContentView::panelKey`.

## Conflicts, and the choice made

Illustrator's key wins wherever the app already had it.

| Key | Figma | Illustrator (kept) | What Figma's action gets |
|---|---|---|---|
| R | Rectangle | Rotate | Rectangle is M |
| L | Line | Ellipse | Line is `\`; Ellipse also answers Figma's O |
| C | Comment | Scissors | no comments in the app |
| S | Slice | Scale | Scale also answers Figma's K |
| A | Frame | Direct Selection | Frame is F |
| N / Shift+N | Next / previous frame | Pencil | Shift+PageDown / PageUp move between artboards; Alt+PageDown / PageUp between pages |
| Ctrl+D | Duplicate | Transform Again | Duplicate is Ctrl+Alt+D (or Alt+drag) |
| Ctrl+R | Rename | Rulers | rename from the Layers panel (double-click) |
| Ctrl+0 | Zoom to 100 % | Fit Artboard in Window | Shift+0 |
| Ctrl+Shift+V | Paste over selection | Paste in Place | Paste in Place |
| Ctrl+Shift+R | Paste to replace | (unused) | left unbound: no such command |
| Ctrl+Y | Redo (Windows) | Outline view | Redo is Ctrl+Shift+Z |
| Alt+A, D, W, S, H, V | Align left, right, top, bottom, centres | menu mnemonics (Alt+E Edit, S Select, V View, W Window, H Help…) | not bound: four of six clash with the menu bar's mnemonics, and half a set is worse than none. Align is in the Properties panel, the right-click menu and Ctrl+K |
| Alt+, Alt+. | Letter spacing | Alt+Left / Alt+Right | tracking keeps Illustrator's |
| Ctrl+Shift+L | Lock | Align left (type; not built yet) | Lock. Bring the type key back with a different chord |
| X | Swap fill and stroke (Shift+X) | X switches focus, Shift+X swaps | X swaps, as it already did |

## The comparison

"Before" is the app at the start of this pass; "Now" is what it does after.

### Tools

| Figma | Action | Before | Now |
|---|---|---|---|
| V | Move | V | V |
| F | Frame | F | F |
| P | Pen | P | P |
| Shift+P | Pencil | N only | N, and Shift+P |
| T | Text | T | T |
| R | Rectangle | M (R is Rotate) | conflict: M |
| O | Ellipse | L | L, and O |
| L | Line | `\` (L is Ellipse) | conflict: `\` |
| K | Scale | S | S, and K |
| I | Eyedropper | I | I |
| H | Hand | H | H |
| Z | Zoom | Z | Z |
| Space (hold) | Hand while held | Space | Space |
| C, S, A | Comment, Slice, Frame | Scissors, Scale, Direct Selection | conflicts: Illustrator's |
| B | Paint bucket | (no tool) | not built |

F also makes Browser Views: a frame's **Browser View switch** (on the canvas,
Object ▸ Browser View ▸ Turn On/Off Browser View, and Ctrl+K) turns its live
page on and off (BROWSER-VIEW.md, "The Browser View switch"). The separate
Browser View tool is gone; it never had a key, and an old remapping or agent
that asks for it gets Frame. The Browse tool (the arrow in the Selection slot)
still has no key and ends with Esc.

### Selection

| Figma | Action | Before | Now |
|---|---|---|---|
| Ctrl+A | Select all | Ctrl+A | same |
| Esc | Deselect | only left isolation | leaves isolation first, then clears picked anchors, then the selection |
| Enter | Select children; edit type | pen finish only | into the selected groups and frames; opens selected type for editing |
| Shift+Enter | Select parent | none | out to the parent group or frame |
| Tab / Shift+Tab | Next / previous sibling | moved keyboard focus | walks the siblings, wrapping; with nothing selected it still moves focus |
| Ctrl+Alt+] / [ | (Illustrator's) next object above / below | same | same |
| Shift+click | Add to selection | works | works |
| Arrows, Shift+arrows | Nudge, nudge ×10 | worked from the canvas | also from a plain button; step is the Keyboard Increment preference |
| Alt+drag | Duplicate | works | works |
| 1…9, 0 | Opacity 10 %…100 % | works | works, from a panel button too |

### Edit

| Figma | Action | Before | Now |
|---|---|---|---|
| Ctrl+Z, Ctrl+Shift+Z | Undo, redo | same | same |
| Ctrl+X, C, V | Cut, copy, paste | same | same |
| Ctrl+Alt+C, V | Copy, paste properties | same | same |
| Ctrl+D | Duplicate | Transform Again | conflict: Ctrl+Alt+D |
| Delete, Backspace | Delete | same | same, from a plain button too |
| Ctrl+Shift+E | Export | Export PNG is Ctrl+Alt+E | Export for Screens is Ctrl+Shift+E |
| Ctrl+K, Ctrl+/ | Actions menu | Ctrl+K, Ctrl+/ | same |
| Ctrl+Alt+Shift+L | Lock / Unlock Document | none | File ▸ Lock Document (see below) |

### Arrange and layout

| Figma | Action | Before | Now |
|---|---|---|---|
| Ctrl+G | Group | same | same |
| Ctrl+Shift+G | Ungroup | same | same |
| Ctrl+Alt+G | Frame selection | same | same |
| Ctrl+] / Ctrl+[ | Bring forward, send backward | same | same |
| Ctrl+Shift+] / [ | Bring to front, send to back | same | same |
| Shift+A | Add auto layout | canvas focus only, no menu key | Object menu entry, works from any focus |
| Alt+Shift+A | Remove auto layout | canvas focus only | Object menu entry |
| Shift+H, Shift+V | Flip horizontal, vertical | none | Object ▸ Transform entries |
| Ctrl+Shift+L | Lock / unlock | Ctrl+2 | Ctrl+2 and Ctrl+Shift+L (*fixed*) |
| Ctrl+Shift+H | Show / hide | Ctrl+3 | Ctrl+3 and Ctrl+Shift+H (*fixed*) |
| Ctrl+Alt+M | Use as mask | Ctrl+7 | Ctrl+7 and Ctrl+Alt+M (*fixed*) |
| Ctrl+Alt+K, Ctrl+Alt+B | Create component, detach instance | same | same |
| Alt+A/D/W/S/H/V | Align | none | conflicts with mnemonics; see above |
| Ctrl+Alt+Shift+K | Tidy up | none | not built |

### View and zoom

| Figma | Action | Before | Now |
|---|---|---|---|
| Ctrl+= / Ctrl+- | Zoom in, out | same | same |
| Shift+1 | Zoom to fit | Shift+1 (and Ctrl+0) | same |
| Shift+2 | Zoom to selection | Shift+2 (and Ctrl+Alt+0) | same |
| Shift+0 | Zoom to 100 % | Ctrl+1 only | Ctrl+1 and Shift+0 (*fixed*) |
| Ctrl+' | Pixel grid / grid | Ctrl+' | same |
| Alt+1 | Layers panel | F7 | F7 and Alt+1 (*fixed*) |
| Alt+8 | Design (Properties) panel | none | Alt+8 |
| Ctrl+\\ | Show / hide the interface | none | not built |
| Ctrl+Shift+? | Keyboard shortcuts | none (Edit menu, no key) | Help ▸ Keyboard Shortcuts…, Ctrl+Shift+? (and Ctrl+Shift+/) |
| Ctrl+scroll, Alt+scroll | Zoom | works | works |

### Type

| Figma | Action | Before | Now |
|---|---|---|---|
| Ctrl+Shift+> / < | Font size | same | same |
| Ctrl+B, I, U | Bold, italic, underline | none | not built as keys: styles are picked in the Character section |
| Ctrl+Alt+L, T, R, J | Align text | none | not built as keys: alignment is in the Paragraph section |
| Alt+Shift+, / . | Line height | Alt+Up / Alt+Down | Illustrator's keys stay |

## Lock Document

File ▸ Lock Document makes the file read-only until it is unlocked. Its key is
Ctrl+Alt+Shift+L, and it is also in the Ctrl+K palette, like every menu
command. Ctrl+K itself is the command palette, so it can't be the key. Figma's Ctrl+Shift+L locks the *selection*, so the whole
document is the same key with Alt held: a heavier lock on the same finger
pattern, and it sits with Share's Ctrl+Alt+Shift+S. It is remappable like any
menu key.

While locked, `EditorSession` turns away every edit at one gate
(`edit`, `beginEdit`, `beginInteraction`, undo and redo, and type edits) with
one status line, so new operations are covered without their own check. The
agent's edits fail with error `-32004` and a message that says to ask the user
to unlock. Selecting, inspecting, measuring, saving, exporting and sharing
still work. The lock is saved in the `.omai` (`"locked": true`, absent when
unlocked) and the tab shows a lock. Locking is not an undo step, but it marks
the file unsaved.

## The fixed keys

A *fixed* key is a second key on a menu entry. The sheet remaps the entry's
first key; the second stays put. They are Ctrl+Shift+L, Ctrl+Shift+H,
Ctrl+Alt+M, Shift+0, Alt+1, Ctrl+Shift+/ , Ctrl+/ , Shift+1 and Shift+2.
Enter, Shift+Enter, Tab, Shift+Tab and Esc on the canvas are fixed too.

## How to add a key

- A command in a menu: give its `Menus::add` a key, add one line to
  `ShortcutDefinition::all()` (`entry(title, key, modifiers, true)`), and the
  menu, the tooltip, the palette and the sheet all show it.
- A tool: add it to `toolKeys` (or `toolAliases` for a second key) in
  `KeyboardShortcuts.cpp`.
- A button that has a menu equivalent: the task bar's `ActionButton` reads the
  key off the entry. Other buttons name the key in their tooltip with
  `ShortcutSettings::tip`.
