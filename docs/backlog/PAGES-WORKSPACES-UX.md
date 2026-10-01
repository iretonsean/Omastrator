# Pages as Workspaces: the UX (queued, medium-high priority, 2026-09-30)

**Status: queued next, ahead of the backlog.** The author, 2026-09-30: "the UX still doesn't feel right."
Read docs/WORKSPACES.md first (how it works today, and its "First live run" and "In the bar" sections).

## What the author said (2026-09-30, AskUserQuestion)

- **Keep the model:** a page is a real Omarchy workspace, reached with the usual keys, gestures and the bar.
  Fix how it feels; don't move pages back inside the window.
- **Switching feels abrupt.** Going to another page is an instant cut (Omarchy turns workspace animations off),
  so there's no sense of moving between pages.
- **The Pages panel in the main window takes up space,** which defeats the purpose of using workspaces: with
  pages on workspaces, the window shouldn't also spend room on a page list.

## To work out (interview the author with AskUserQuestion, with previews, before building)

- **Motion between pages.** Options to prototype and show the author:
  - Hyprland's own workspace slide, only for page workspaces, if Hyprland 0.56's Lua config can scope it (a
    runtime rule like the stand-in rule, gone at reload); else for all workspaces only if the author wants that
    (it changes his desktop).
  - An in-window transition: after the cut, the canvas slides or cross-fades from the old page to the new one
    (the stand-in's picture and the grab make this possible without faking the desktop).
  - Direction that matches the bar dots and the keys (left/right in page order).
- **Where the page list goes when pages are workspaces.** Options:
  - hide the Pages section of the left panel while Pages as Workspaces is on (the bar dots, Alt+PageUp/Down and
    Ctrl+K cover switching), and keep add/rename/reorder/delete in the Object ▸ Pages menu and the dots' menu;
  - a compact page strip in the document-tab row instead of the panel section;
  - richer bar dots (names on hover today; a right-click menu with New/Rename/Delete Page).
- Known rough edges to fix along the way (WORKSPACES.md): Super+Tab walks the pages in reverse (the author said
  it doesn't matter, but a fix to creation order may fall out of this work), stand-ins and the "Spare" window
  show in window lists.

## Done when

The author has picked the motion and the page-list design from previews, it's built with tests, and he has tried
it on his desktop and says it feels right.
