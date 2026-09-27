# Cloud storage

Omastrator opens and saves documents on cloud services as easily as on this
computer. Every service goes through [rclone](https://rclone.org), so one small
piece of code reaches all of them, and rclone keeps every sign-in.

## What the user sees

- **Connecting.** File ▸ Connect Cloud Storage…, or Cloud Storage… on the
  welcome screen. Choose a service and press Connect. Services that sign in
  through the browser (Google Drive, Dropbox, OneDrive, Box, pCloud) open the
  browser and need nothing typed. The others ask for a few fields (S3, B2,
  WebDAV, SFTP, iCloud Drive, Proton Drive, Mega). Anything rclone asks along
  the way, such as a two-factor code or which drive to use, appears as one
  question at a time. Disconnect makes rclone forget the sign-in; nothing on
  the service is deleted.
- **No rclone.** The sheet says "rclone isn't installed" and offers Install
  rclone, which runs `omarchy pkg add rclone` in a terminal. The sheet moves on
  by itself once rclone appears. Until something is connected, Open, Save As,
  Place and Export are the usual file dialogs and nothing else changes.
- **Open, Save As, Place, Export.** Once a service is connected, these show a
  location list: This Computer (the usual dialog) and each connected service,
  browsed in place, with breadcrumbs, New Folder and Refresh. The dialog
  remembers the last location and folder.
- **Working on a cloud document.** The document opens from a copy in
  `$XDG_CACHE_HOME/omastrator/cloud/<remote>/<path>`. Save writes that copy
  atomically, as any save does, then uploads it in the background. The toolbar
  shows "Uploading to Google Drive…", then "Saved to Google Drive". The tab's
  tooltip shows `remote:path`.
- **Offline.** A failed upload retries by itself, from 2 seconds and doubling
  up to 5 minutes, with Retry Now in the toolbar. A document opened while
  offline uses the copy from last time and says so.
- **Someone else changed it.** Before each upload, Omastrator compares the
  remote file with the version it opened (by hash when the service gives one,
  otherwise by size and time). If they differ, it uploads nothing and asks:
  - **Keep Both** uploads yours beside theirs as
    `name (conflict 2026-09-27 1412).omai`, and the tab follows your copy.
  - **Overwrite** replaces their version with yours.
  - **Open Theirs** opens their version in a new tab and keeps yours open as
    an unsaved "(yours)" document.
  - **Decide Later** leaves it; Resolve… in the toolbar asks again.
- **Closing.** Closing a document whose upload hasn't finished waits for an
  upload that's under way. If the upload is failing or in conflict, it asks:
  Keep Open, or Close Anyway (the save stays in the cache).
- **Recent files.** Cloud documents are listed as `logo.omai — Google Drive`
  with the service's badge, in Open Recent and on the welcome screen, and
  reopen from the service.

## Services

The Connect list shows these first, and Other takes any rclone backend by
name (FTP, Koofr, Seafile, Azure Files and the rest):

| Service | rclone type | Sign-in |
| --- | --- | --- |
| Google Drive | `drive` | browser |
| Dropbox | `dropbox` | browser |
| OneDrive | `onedrive` | browser |
| iCloud Drive | `iclouddrive` | Apple ID, password, then a 2FA code |
| Box | `box` | browser |
| Proton Drive | `protondrive` | username, password, 2FA code if on |
| pCloud | `pcloud` | browser |
| Mega | `mega` | email, password |
| Amazon S3 and compatible (Cloudflare R2, Wasabi, MinIO…) | `s3` | access key, secret, endpoint |
| Backblaze B2 | `b2` | key ID, application key |
| Nextcloud or WebDAV | `webdav` | URL, user, password |
| SFTP | `sftp` | host, user, password or key file |
| Other | any | rclone's own questions |

Remotes the user made with `rclone config` show up too, under their own names.

Google Drive: rclone's shared client ID is being retired, and rclone asks
whether to keep using it. The sheet shows that question as rclone words it.
Yes goes on to the browser sign-in. No asks for your own client ID and secret
([rclone's guide](https://rclone.org/drive/#making-your-own-client-id)), which
is the setup that lasts.

## Credentials

Omastrator never reads, keeps or logs a credential:

- Sign-ins live in rclone's own config (`~/.config/rclone/rclone.conf`), which
  Omastrator never opens. Passwords are passed with `--obscure`, so rclone
  never writes them in the clear.
- Omastrator's settings keep only remote names and their types (for badges),
  recent `remote:path` entries, and the last folder browsed.
- rclone's standard output is never logged. Errors shown or logged pass
  through a scrubber that hides anything token-like.
- Password fields are cleared as soon as they're sent.
- One limit: rclone takes a new remote's settings as command-line arguments,
  so a typed password is briefly visible to other processes of the same user
  while `rclone config create` runs. Browser sign-ins never pass through
  Omastrator at all.

## Code

- `src/Cloud` (`oma_cloud`):
  - `CloudStorage` runs rclone (`listremotes`, `lsjson`, `copyto`, `mkdir`,
    `config create/update/delete` with `--non-interactive`, and `link` and
    `deletefile` for Share with client, docs/SHARE.md).
  - `CloudLocation` is `remote:path`, and `CloudStamp` is a version to compare
    against. `CloudCache` holds the cache paths and the stamps recorded for
    them.
  - `CloudUploader` runs the background uploads: check, upload, backoff and
    conflicts.
  - `CloudProviders` is the Connect list.
- `src/UI`:
  - `CloudStorageSheet` is the Connect sheet.
  - `CloudBrowser` is the location list and remote browser.
  - `CloudBadge` draws the service badges.
  - `ProjectWorkspace+Cloud.cpp` ties cloud tabs, uploads, conflicts and
    closing together.
- `OMASTRATOR_RCLONE` replaces the rclone binary; `OMASTRATOR_TERMINAL`
  replaces the terminal Install rclone opens.

## Tests

- `tests/Cloud/FakeRclone.cpp` is a stand-in rclone. Its remotes are folders
  and its config holds a fake token, which the tests check never shows up
  anywhere. `FAKE_RCLONE_FAIL` makes chosen commands fail (`offline` fails them
  all).
- `CloudStorageTests` covers parsing, errors and scrubbing, listing,
  transfers, connecting, uploads, conflicts and retries.
- `CloudWorkspaceTests` covers the sheet, the browser and the workspace flows.
- `CloudRcloneTests` runs the real rclone against a `local` remote in a
  throwaway config (`RCLONE_CONFIG` and `--config`), and is skipped without
  rclone. No test touches the user's rclone config.
