# Share with client

Decided 2026-09-27, from VISION.md's fifth principle: get the design in front
of the customer in one step, and bring their feedback back just as fast.

## What the user sees

- **One press.** Share sits at the top right of the window. It is also
  File ▸ Share (Ctrl+Alt+Shift+S, which Illustrator uses for Save for Web;
  remappable), `Share` in Ctrl+K, and Share Selection in the canvas's
  right-click menu and the task bar's ⋯. It shares the artboard, or the
  selection when something is selected. The button's tooltip says which: "Share
  the selection (2 objects)". The link goes on the clipboard, and a toast at the
  top right says "Link copied — Google Drive", then "The selection (2 objects)
  as PNG at 2×.", with Open and Copy Again. No dialog opens on the way.
- **The selection** is rendered alone, cropped to its bounds (strokes
  included), on clear paper. Its layer, groups and any clipping path above it
  come along, so it looks as it does on the artboard.
- **Options.** The arrow beside Share opens a popover: what it shares, Format
  (PNG at 2× by default, PDF, SVG), To (the destinations available now), and
  one line on who can open the link. Changing either is remembered for that
  document. File ▸ Share Options… opens the same popover.
- **Where it goes.** The document's remembered destination if it's still
  there, otherwise the first of:
  1. A Live session that's running: its site (see Live below).
  2. A connected rclone remote whose backend makes public links (`drive`,
     `dropbox`, `onedrive`, `box`, `pcloud`, `s3`, `b2`, `mega`, `koofr`,
     `jottacloud` and the other backends with rclone's PublicLink). The file
     goes to `Omastrator Shares/<document>/<time>-<id>.<ext>`, then
     `rclone link` makes the link. S3 and B2 links are presigned and stop
     working after about a week; the popover says so.
  3. GitHub through `gh`: SVG (text) as a secret gist; PNG and PDF as the
     asset of a new release in `omastrator-shares`, a public repository on the
     user's account, made with a README the first time. A private repository's
     assets can't be opened without signing in, so it has to be public.
  4. Nothing: the toast says "There's nowhere to share to yet. Connect a cloud
     service or GitHub, then press Share again." with Connect Cloud Storage…
     and Connect GitHub. The popover says the same.
- **GitHub asks once.** Before the first upload to GitHub, a sheet says what
  happens and that neither is private: a secret gist is unlisted but opens for
  anyone with the link, and anyone can browse a public repository's releases.
  Share via GitHub goes ahead and isn't asked again; Cancel uploads nothing and
  asks next time. `gh` not signed in reads "GitHub isn't connected." with
  Connect GitHub.
- **Live.** With a project in Live (running, or the last one Deploy acted on),
  Share copies the latest deploy's URL when it's of the current commit and
  nothing is waiting to be saved. Otherwise, and whenever the popover's Preview
  Deploy is pressed, it runs a preview deploy and copies its URL. The popover
  also has Copy Latest Deploy. A preview deploy never touches production and
  never pushes.
- **Shared Links…** (File menu, Ctrl+K, or Shared… in the popover) lists the
  front document's shares, and the Live project's, newest first: where, the
  format, selection or artboard, the time, and the link, with Copy, Open and
  Unshare. Unshare deletes the cloud file (`rclone deletefile`), the gist
  (`gh gist delete`) or the release and its tag (`gh release delete
  --cleanup-tag`), then drops the row. A deploy can't be taken down from here:
  its row has Remove, which only takes it off the list.
- **Paste Client Feedback…** in the Shared popover opens a text box and a
  choice of which share it's about. Apply Feedback selects what was shared (the
  selection's objects that still exist, or nothing for the whole artboard) and
  runs Edit with Instruction with the client's words as the instruction. The
  agent's result is the usual proposal to keep or discard. There is no comment
  system: the client replies however they like and the designer pastes it.
- **Failures** lead with the plain line ("Google Drive didn't make a link:
  …", "Preview deploy failed: …", with Details for the deploy's log) and may
  carry one dry line after it, each at most once per install (docs/HUMOR.md).

## Send to a device

The other way to get a design in front of someone, for the author's own phone:
File ▸ Send to a Device… (or Ctrl+K, "AirDrop", or the button in the Share
popover) AirDrops the selection, or the artboard when nothing is selected, to
a nearby Apple device.

- **Nothing new to install beyond AirDrop itself.** It uses `omdrop` (the
  netmojo.omdrop plugin: discovery and the sender) and, when it's there,
  `omadrop` (the Omarchy AirDrop front end: it wakes a sleeping iPhone over Bluetooth
  and keeps the omdrop window open). Without omdrop the popover says "AirDrop
  isn't set up on this computer" and how to install the plugin. Without
  omadrop, omdrop sends alone with `--wait 45`.
- **The popover** opens and looks at once: what it sends, Format (PNG at 2×,
  PDF or SVG; remembered), and To, the devices nearby. The device used last
  is chosen first (matched by its address, else its name; kept in QSettings
  as `shareDeviceAddress` and `shareDeviceName`). Look Again searches again.
- **Looking.** `omdrop peers --json` lists what's nearby. When AirDrop is off
  ("currently OFF") it runs `omdrop on 5m` first, which makes this computer
  visible to nearby devices for five minutes; omadrop does the same to send.
  It looks for about ten seconds before saying "No Apple devices nearby. Set
  the iPhone to AirDrop ▸ Everyone for 10 Minutes, unlock it and open its
  share sheet." The names come from `peers -n --json --stream`, a device at a
  time (15 to 20 seconds; unnamed devices show as "Apple device (-48 dBm)"),
  and Send is available meanwhile: sending stops the name lookup first,
  because it transmits.
- **Sending.** `omadrop send --quiet --to <address> --label <name> -- <file>`.
  The file is rendered like a share (the selection cropped to its bounds on
  clear paper) into a temporary folder that goes when the send ends. The
  toast, the same one Share uses, says "Sending to Test iPhone…", "Waiting
  for … to accept…", then "Sent to …" with what was sent. Cancel stops it.
  Failures lead with the plain line: "… declined it.", "… didn't answer. Set
  AirDrop to Everyone for 10 Minutes, unlock it, then send again.", or the
  sender's last line.
- Nothing is recorded in the Shared list: there's no link to keep.

## Where things are kept

- `$XDG_CONFIG_HOME/omastrator/shares.json`, readable only by the user:
  `githubConfirmed`, and per document its format, destination and Shared list
  (link, time, where, format, scope, the selection's object ids, and what
  Unshare needs: the `remote:path`, the gist id, or the repository and tag).
  Documents are keyed by `remote:path` for cloud documents, the absolute path
  for local ones, and `untitled:<tab>` until the first save, when the entry
  moves to the path. A Live project's shares are under `live:<folder>`. Nothing
  is written into the `.omai`.
- Rendered files go to a temporary folder that is removed when the upload ends.
- Preview deploys are Deploy records with `"preview": true`: History doesn't
  mark a commit deployed from one, and `Deploy::latest` finds either kind.

## Credentials and logs

- rclone keeps its sign-ins and `gh` its token; Omastrator reads neither.
- Neither program's output is logged. A failure shows one line of it, passed
  through `CloudStorage::scrub`. Links are never logged either: an S3 link
  carries its signature.
- `gh` runs with `GH_PROMPT_DISABLED=1`, so it fails instead of asking.

## Code

- `src/Cloud/CloudStorage`: `link`, `deleteFile`, `makesLinks`, and
  `refreshRemotes(done)`.
- `src/Live/Deploy`: `resolvePreview` (omastrator.json `"preview"`,
  package.json `deploy:preview` or `preview:deploy`, `vercel deploy`,
  `netlify deploy`, `wrangler pages deploy --branch preview` or
  `wrangler versions upload`), `latest`, `Record::preview`, and
  `dryLine(lines)`. `DeployJob::Plan::preview`.
- `src/UI/Share`: formats, destinations, the store, the selection's document
  and writing the file.
- `src/UI/ShareJob`: one share or Unshare in the background (rclone, `gh`, or
  a preview `DeployJob`).
- `src/UI/ShareController`: the one-press logic, the remembered choices, the
  GitHub question, the notice, Unshare and Paste client feedback. The window
  owns one.
- `src/UI/SharePanels`: the toast, the Share and Shared popovers, and the
  GitHub sheet. `SharePanels+Device.cpp` is the Send to a Device popover.
- `src/UI/DeviceSend`: finds devices and sends through omdrop and omadrop;
  `ShareController+Device.cpp` renders the file, remembers the choice and
  drives the toast. `$OMASTRATOR_OMDROP` and `$OMASTRATOR_OMADROP` replace the
  programs (one that doesn't exist counts as not installed);
  `$OMASTRATOR_DEVICE_RETRY_MS` shortens the wait between looks.

## Tests

`tests/UI/ShareTests.cpp` runs every path with fakes: the fake rclone
(`link` and `deletefile` added), a fake `gh` whose gists and releases are
files, a fake deploy command in a git project whose remote is a local bare
repository, and the fake agent for Paste client feedback. It checks that no
token or link reaches the log. `CloudStorageTests` covers `link` and
`deletefile`; `DeployTests` covers `resolvePreview` and preview records.

`tests/UI/DeviceShareTests.cpp` runs Send to a device against a fake omdrop
(peers, on, names) and a fake omadrop that keeps the file it was given: the
radio is never reached, and the tests set both variables before anything runs.
