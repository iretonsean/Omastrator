# Auto layout

Figma's auto layout on Omastrator's frames. It is item 2 of docs/FIGMA-AUDIT.md.
The editable lift depends on it: a page's flexbox comes in as auto-layout frames.

## Model

A frame may carry an `AutoLayout`:

| Field | Values | Figma |
|---|---|---|
| `direction` | horizontal, vertical | the arrows |
| `gap` | pt, or `auto` for space between | Spacing between items |
| `padding` | left, top, right, bottom (pt) | Padding |
| `primary`, `counter` | start, center, end (counter also baseline later) | the alignment grid |
| `wrap` | on for horizontal only, with `counterGap` between rows | Wrap |
| `width`, `height` | fixed, hug | the frame's own sizing |

Each child of an auto-layout frame may carry a `LayoutItem`:

| Field | Values | Figma |
|---|---|---|
| `width`, `height` | fixed, hug (frames and text), fill | Resizing |
| `absolute` | bool | Absolute position: the child keeps its place and takes no part in the flow |

## Order

Children flow in document order, bottom to top. That is the Layers panel read
from the bottom up, as in Figma, where the first child is the leftmost or
topmost. Hidden children take no space.

## When it runs

`VectorDocument::applyAutoLayout()` lays out every auto-layout frame, innermost
first, so a hugging child frame has its size before its parent measures it. It
runs:

- wherever text is reflowed (`EditorSession::notify`, `pruneSelection`);
- after a file is read.

So every edit, drag preview, undo and load comes out laid out. Saved files
store the laid-out positions too, so older builds still draw them right.

## Laying out one frame

1. Measure each flowing child by its bounds without strokes (Figma measures
   without strokes by default). A fill child's size on the primary axis is
   left open.
2. The primary axis:
   - The free space is the inner size minus the fixed and hug sizes and the
     gaps.
   - Fill children share the free space equally.
   - With `gap: auto` (space between), the free space goes between the items.
   - Otherwise it goes before, around or after them, as `primary` says.
3. The counter axis: each child is aligned in the inner size as `counter`
   says. A fill child stretches to the inner size.
4. Hug: the frame's size along that axis becomes content plus padding. It
   grows from its top-left corner.
5. Wrap: items go into rows while they fit the inner width. Rows are stacked
   with `counterGap` and aligned in the frame.

Resizing a child:

- A frame resizes its box.
- A live rectangle resizes its rect.
- Area type changes its width. Point type can't fill, so it hugs.
- Anything else scales about its top-left corner.

Moving is a translation of the child's subtree.

## Editing

- **Keys:** Shift+A and Alt+Shift+A are Object menu entries, so they work with any panel
  focused and show beside the entry. Typing keeps its capital A.
- **Shift+A with a frame selected:** adds auto layout, choosing the direction
  from how its children are spread.
- **Shift+A with loose objects selected:** wraps them in a new auto-layout
  frame, hugging, whose gap and padding come from the current spacing.
- **Alt+Shift+A:** removes auto layout; the children stay where they are.
- **The Properties panel's Auto layout section:**
  - direction;
  - gap (a number or Auto);
  - padding (per side, or two linked);
  - the 3 × 3 alignment grid;
  - wrap;
  - width and height sizing (Fixed, Hug, Fill for a child of an auto-layout
    frame);
  - Absolute position.
- **Dragging a child inside an auto-layout frame** reorders it. It can't be
  placed freely unless it's absolute. (This lands after the panel.)
