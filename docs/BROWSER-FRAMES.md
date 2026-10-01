# Browser View frames and canvas workspaces (design, 2026-09-28)

**Status: agreed with the author on 2026-09-28, including the revised profile plan (a headless Omastrator profile). Nothing is built.** This
replaces the island's Live mode once it ships. Read it alongside VISION.md
("close to the customer") and ANYWHERE.md.

## The workflow

1. **Make a canvas workspace.** A document's pages are Omarchy workspaces:
   "New Page" claims a workspace and opens the canvas there, full screen,
   with the Omarchy bar and the island left visible. Super+number, or a
   swipe, moves between pages the same way it moves between workspaces.
2. **Draw a frame and turn its Browser View on.** The Frame tool (F) does it:
   a frame's Browser View switch (2026-09-29, BROWSER-VIEW.md "The Browser
   View switch") makes it a frame with a browser's controls on top: an
   **editable address bar** for any URL, back, forward, reload and breakpoint
   buttons. On runs the project's localhost server with the live code; off
   freezes it until it's back on or the app closes.
3. **It shows the live site.** Design inside it: draw over the site, or paste
   content lifted from another browser window.
4. **Resize it to check breakpoints.** Drag the frame's edge or corner: the
   site reflows while you drag, and the frame keeps its new size. The
   breakpoint buttons preview the project's own breakpoints and don't change
   the frame; the dotted one is the frame's own width, and pressing it comes
   back.
5. **Build it and ship it.** "Build it" hands the design to the user's
   default agent. The agent writes it into the project's code on a branch,
   and the change waits under Review changes. **Deploy** publishes it to the
   live host with the project's own setup. Direct Live edits (text, colour,
   spacing, size, type and radius from the element bar, snapped to the
   project's tokens) go the same way.

*(2026-09-29: the whole island is removed; see OS-SUITE.md, component 1.)*
When this ships, **Live mode is removed from the island.** The island's
Live button opens a Browser View instead.

## Decisions (the author, 2026-09-28)

- **Pages, not artboards, are workspaces.** A page is a canvas that holds
  artboards and frames (Figma's Pages, which Omastrator doesn't have yet;
  see FIGMA-AUDIT.md). Pages use Hyprland **named workspaces**
  (`design:<document> · <page>`), so the user's numbered workspaces are
  never taken over.
- **The address bar is a real, editable field.** Type any site to view it
  and edit it. Sites that aren't the user's keep today's rules: edits stay on
  this machine and there's no Deploy (ANYWHERE.md, "Sites that aren't yours").
- **The Browse tool** is an arrow in the tool rail.
  - **While it's on:** the Browser View acts as a browser, and clicks,
    scrolling and typing go to the site, so nothing in the design can be
    grabbed by accident.
  - **Every other tool designs,** and Esc returns to Selection.
  - **The frame's own controls** (address bar, back, forward, reload,
    breakpoints) work with any tool.
- **Dragging an edge resizes the frame, as it does any frame.** The page
  reflows during the drag, and releasing keeps the width and the height as one
  undo step ("Resize"). The design width is the frame's width, so it follows.
  **A breakpoint button is the preview:** it makes no undo step, and pressing
  the dotted button (the frame's own width), the same button again, or Esc
  returns to the frame's size. The objects inside follow one of three rules,
  chosen per object:
  - **pinned to a page element**, following it as the page reflows (Lift
    knows which element each shape came from);
  - **following the frame's constraints and auto layout**, like any frame's
    children;
  - **fixed**, with its design left alone at the design width. (This rule
    applies to a breakpoint preview. A kept resize moves children by their
    constraints, as a plain frame's resize does.)
- **Breakpoint buttons** come from the project's own breakpoints (its
  Tailwind theme or CSS media queries, read the way token snapping reads
  them). Otherwise they default to 390, 768, 1280 and 1440.
- **Duplicate at Breakpoints** lays the same URL out as frames side by side,
  one per breakpoint, sharing one design layer. A note added in one shows in
  all of them.
- **Profile:** a separate Omastrator profile running headless, signed in once in a normal window. The author approved it after the rendering test ruled out using their own profile. See "Revised profile plan".

## The browser behind the frame

- **The frame doesn't embed a window.** Wayland doesn't let one app embed
  another app's window. The frame **streams** a real Chromium tab through
  the DevTools protocol, the same link Live uses today:
  - `Emulation.setDeviceMetricsOverride` sets the frame's width and height,
    which is how DevTools' device mode works, so any width is instant.
  - `Page.startScreencast` sends the pixels, at a resolution that follows the
    canvas zoom.
  - In the Browse tool, input goes back through `Input.dispatch*`.
- **When it pauses.** Frames that are off screen, or zoomed out below a
  threshold, pause and show their last frame.
- **What already works on it.** Lift, Inspect, the Live element bar and
  token snapping already read the tab's page, so they work on the frame
  without new plumbing.

### Profile: the user's own (default) vs a separate Omastrator profile

| | The user's own profile (a tab in their Chromium) | A separate Omastrator profile |
|---|---|---|
| Logins, bookmarks, passwords | Already there | Signed in separately. Google sign-in and sync **do** work: the author's `~/.config/chromium-flags.conf` passes the OAuth client, and a profile launched through the same flags gets it. Password-manager extensions work too. |
| Setup | The Omastrator extension is already loaded by chromium-flags.conf | Omastrator launches it |
| The user's browsing | The frames' tabs live in their own window, kept out of sight | Completely separate |
| Chromium running | A profile can only be open in one process, so Omastrator uses the user's running Chromium, or starts it | Independent |
| Rendering out of sight | **The unknown the test answers** | Launched with flags that keep it drawing |
| "Is debugging this browser" banner | Shown on the user's browser while a frame is attached through `chrome.debugger` | Never (it uses a debugging port) |
| The agent and privacy | Frames are logged in, so screenshots handed to the agent can show account pages | Only what's signed in there |
| First-visit testing | Needs a toggle | The default |

**Plan:** the user's own profile is the default, and a per-frame
**Clean Session** switch uses the separate profile for first-visit testing
and for keeping logged-in pages away from the agent. If the rendering test
fails for the user's own profile, the separate profile becomes the default.
It then offers to copy the user's bookmarks (a plain file), and signing in
covers the rest. Passwords are never copied between profiles.

## Rendering test (2026-09-28)

The question: does a Chromium tab keep producing screencast frames, and obey
a width override, while its window is somewhere nobody can see it (a
Hyprland special workspace), with and without Chromium's anti-throttling
flags? The results are below.

Run on the author's machine: Chromium 153 on Arch Linux ARM, Hyprland
0.56.2, Wayland. A throwaway profile, a test page with a
requestAnimationFrame counter, the width set to 390 × 844 at 2×, and a
DevTools screencast measured over 5 seconds:

| Where the tab's window was | Screencast | Animation ticks | Usable? |
|---|---|---|---|
| A hidden Hyprland special workspace (default flags) | 1 frame, then nothing | 0 | **No: it stops drawing** |
| The same, with `--disable-backgrounding-occluded-windows --disable-renderer-backgrounding --disable-background-timer-throttling` | 1 frame | 0 | **No:** the flags don't help |
| Floating off screen (-4000, -4000) on the visible workspace | 12.8 fps | 62 | Barely: throttled to about 12 fps, and the window has to follow the user's workspace |
| The same, with the flags above | 11.8 fps | 56 | Same |
| `--headless=new` (no window at all) | **53 fps** | 300 | **Yes** |

What this means:
- **A tab in a window the user can't see is starved of frames.** Hyprland
  stops sending it frame callbacks, and Chromium's anti-throttling flags
  can't override that. So "the user's own Chromium, kept out of sight"
  doesn't work, and parking it off screen only gives about 12 fps.
- **Headless Chromium draws at full speed.** A profile can only be open in
  one Chromium at a time, so a headless browser **can't use the user's own
  profile while their Chromium is open**. That means a separate Omastrator
  profile.
- **The width override works**, with screencast frames at 390 × 844 and
  780 × 1688 at 2×. The test page reported `innerWidth` 980 because
  `mobile: true` without a viewport meta tag uses the 980 px layout
  viewport. Frames should send `mobile: false` unless the user picks a
  device, or real pages without a viewport tag won't hit their breakpoints.

### Revised profile plan (approved by the author, 2026-09-28)

The test ruled out the first choice, and the author approved this instead:

- **Default:** a **separate Omastrator profile running headless**,
  set up once to feel like the user's own:
  - **Sign in once:** the first Browser View opens that profile in a normal
    window, with a "Sign in to Omastrator's browser" prompt. Google sign-in
    and sync work because it launches through the same
    `chromium-flags.conf`, which brings bookmarks and passwords with it. Any
    site logins made there persist, and later runs are headless.
  - **Bookmarks** can also be imported from the user's profile, since
    they're a plain file. Passwords are never copied.
  - **Limit:** extension pop-ups, such as a password manager's, open as
    their own windows, so they don't appear inside a streamed frame. Sync's
    autofill works.
- **Disk use is capped.** The profile lives for good in
  `~/.local/share/omastrator/browser`, so `Browser::chromiumArguments` starts
  Chromium with `--disk-cache-size=67108864` and `--media-cache-size=67108864`
  (64 MB each; Chromium treats them as limits it trims to, not exact sizes).
  Browser View must go through that function so it keeps the cap; the
  headless runs tests use keep their 1-byte caches. The profile's cookies and
  logins are never trimmed.
- **Clean Session** stays a per-frame switch, now a separate, empty
  headless profile.
- **The user's own browser** stays reachable. "Open in My Chromium" sends
  the frame's URL there, and Lift and Inspect keep working on it through
  the extension, as today.

## Escape hatches (the author's standing rule)

- Super+number always leaves a page workspace, and a page never grabs the
  keyboard.
- The Browse tool ends with Esc.
- `omastrator reset` and Super+Alt+Escape release every claimed workspace
  and every frame's tab.
- Closing a document gives its workspaces back and closes its frames' tabs.

## Build order (after the import branches merge)

1. **Pages** in the document model and file format (`.omai`), with the
   Pages list in Layers and Ctrl+K. It's useful even without workspaces.
2. **Canvas workspaces:** pages claim named workspaces, and closing a
   document gives them back. **Built** (View ▸ Pages as Workspaces, off by
   default; WORKSPACES.md section 7). Whether Super+Tab reaches the named
   workspaces on a live desktop is still to be checked by hand.
3. **The Browser View frame:** the tool, the frame controls, streaming, the
   Browse tool, breakpoint buttons and resize-as-preview. **Built** (the
   Object ▸ Browser View menu and, since 2026-09-29, the Frame tool's Browser
   View switch in place of a separate tool; the
   design and what was decided while building are in BROWSER-VIEW.md).
4. **Live inside the frame:** the element bar, tokens, Review changes,
   History, Deploy, and "Build it" through the agent. **Built**, apart from
   the island (Edit Page, the element bar, the dev server behind the frame,
   Deploy, Save, Review Changes, History and Build It; the design and what was
   decided while building are in LIVE-IN-FRAME.md). Removing Live mode from
   the island is still to do, and until then the island's Live row and Live's
   own window work as they did.
5. **Duplicate at Breakpoints,** the three pinning rules, and Clean Session.
