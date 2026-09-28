# Releasing

The author's steps to cut a release. Nothing here runs itself: no agent pushes
a tag, publishes a release or pushes to the AUR.

## Cutting a release

1. On `main`, with everything you want in the release already merged, tag it
   and push the tag:
   ```sh
   git tag v0.2.0
   git push origin v0.2.0
   ```
2. `.github/workflows/release.yml` picks up the `v*` tag: it builds x86_64 and
   aarch64 in Arch containers, runs the tests offscreen, and attaches
   `omastrator-<version>-x86_64.tar.zst`, `omastrator-<version>-aarch64.tar.zst`
   and `SHA256SUMS.txt` to a GitHub Release. Watch it in the Actions tab; a
   full run (two Qt builds) takes a while.
   - **First time only:** the aarch64 job runs on a native `ubuntu-24.04-arm`
     GitHub-hosted runner inside `menci/archlinuxarm:base-devel` — there's no
     official Arch Linux ARM image, so this is a community one, unverified
     from here (no GitHub Actions run happened while writing this). If it
     can't pull that image or `pacman -Syu` fails oddly in it, swap in a
     working ALARM image, or fall back to QEMU user-mode emulation of aarch64
     inside the same `archlinux:base-devel` image the x86_64 job uses (slower,
     but known to work).
3. Once the release is published, update `omastrator-bin`'s PKGBUILD:
   ```sh
   scripts/update-bin-pkgbuild.sh 0.2.0
   ```
   This fills in `pkgver`, downloads that release's `SHA256SUMS.txt` and
   writes `sha256sums_x86_64`/`sha256sums_aarch64`, then regenerates
   `packaging/aur/omastrator-bin/.SRCINFO`. Check the diff, then commit it on
   `main`.
4. Push both packages to their AUR repositories (see the checklist below for
   the first time). From each package's own clone of its AUR repo:
   ```sh
   cp packaging/aur/omastrator-git/{PKGBUILD,.SRCINFO,omastrator.install} <clone-of-aur/omastrator-git>/
   cd <clone-of-aur/omastrator-git> && git commit -am "Update" && git push
   ```
   and the same for `omastrator-bin`. `omastrator-git`'s `pkgver` is computed
   by `makepkg` itself from the live `main` branch on every build, so it
   doesn't need updating here — only bump `pkgrel` if you changed the
   PKGBUILD without a new commit landing on `main`.
5. Update the README's Install (alpha) section once the AUR packages are
   live (see below): drop "once published".

## First-time AUR account and SSH key checklist

1. Create an account at [aur.archlinux.org](https://aur.archlinux.org).
2. Generate an SSH key for it if you don't already have one you want to use
   for this (`ssh-keygen -t ed25519 -C "aur"`), and add the public key to the
   account's SSH Public Keys.
3. Add to `~/.ssh/config`:
   ```
   Host aur.archlinux.org
       IdentityFile ~/.ssh/<the key you added>
   ```
4. For each of the two packages, once, from an empty directory:
   ```sh
   git clone ssh://aur@aur.archlinux.org/omastrator-git.git
   git clone ssh://aur@aur.archlinux.org/omastrator-bin.git
   ```
   The first push to each creates the AUR package page.
5. Commit as yourself there, not as the repo's `noreply` address — the AUR
   wants a real contact.

## Local testing

`packaging/aur/omastrator-git/` and `packaging/aur/omastrator-bin/` are
tracked in this repo so they can be tested here before they ever reach the
AUR. To test `omastrator-git` locally (never as root, and under the build
lock — AGENTS.md):
```sh
cd packaging/aur/omastrator-git
flock ~/.cache/omastrator-build.lock env MAKEFLAGS=-j2 makepkg -f
tar -tf omastrator-git-*.pkg.tar.* | sort   # check the layout
namcap omastrator-git-*.pkg.tar.*           # if namcap is installed
```
This clones the real `main` branch from GitHub (not this worktree), builds it
Release, and packages it — the same thing an alpha tester's `yay -S
omastrator-git` would do. `makepkg`'s working files (`src/`, `pkg/`, the
built package) are git-ignored.

`omastrator-bin` can only be tested locally once a real release exists to
download, since its source is the release tarball.

## Design decisions and why

- **Depends `qt6-base`, `jq`, `shared-mime-info`, `hicolor-icon-theme` and
  `desktop-file-utils`.** `qt6-base` covers Widgets, Concurrent, Network and
  Test — Omastrator doesn't need `qt6-declarative` itself (only
  `ShellPluginTests` links it, optionally, to run `OverlayLogic.js`); the
  shell plugins run inside `omarchy-shell`/quickshell, which brings its own
  Qt Qml. `jq` is a hard dependency of `omastrator setup` (`Setup+Files.cpp`
  shells out to it to edit `shell.json`), not optional. The other three back
  the `.install` file's `update-desktop-database`, `update-mime-database` and
  `gtk-update-icon-cache` calls, which a direct `cmake --install` runs itself
  (see the `install(CODE ...)` block in `CMakeLists.txt`) but a packaged
  install has to run from post-install hooks instead.
- **`optdepends` follow the real `OMASTRATOR_*` overrides in AGENTS.md**,
  cross-checked against where each program is actually run
  (`src/Agent/Capture.cpp`, `src/Cloud/CloudStorage.cpp`,
  `src/Live/History.cpp`, `src/Anywhere/Inspect.cpp`), plus
  `qt6-imageformats` for placing TIFF and WebP images
  (`src/IO/ImageImporter.cpp` reads `QImageReader::supportedImageFormats()`,
  and Qt ships those two as plugins in that package, not in `qt6-base`).
  **`ghostscript` isn't in the list.** The brief that asked for this package
  named it (EPS/PS import through Ghostscript), but nothing in this branch's
  source runs `gs` — that work is still on `feat/import-pdf`, unmerged. Add
  it once that lands; until then it would be an unused dependency.
  **`voxtype` and `omdrop`/`omadrop` aren't in `optdepends` either**: none of
  the three are pacman or AUR packages. voxtype is Omarchy's own dictation
  engine (`omarchy voxtype install`); omdrop/omadrop are an Omarchy shell
  plugin (`omarchy plugin add
  https://github.com/brentkearney/omdrop-plugin.git`). `omastrator setup`
  already names each missing program and how to install it
  (`Setup+Files.cpp`'s `missingTools()`, and `DeviceSend::missingText()`).
- **Built in Arch containers, not a generic Linux image, and not pinned to
  Qt 6.4.** Omastrator only supports Arch/Omarchy. Building where it runs
  means the release binary uses the same rolling-release Qt and glibc a
  tester's system has, so there's no cross-version Qt ABI to reason about —
  and there's nothing to reason about there anyway, since nothing in the
  source uses Qt's private API (checked with a grep for `private/` Qt
  headers; `QPdfWriter` and everything else here is public API). The
  trade-off: a binary built this way assumes the tester's Arch is reasonably
  current, same as the from-source install already does.
- **`packaging/aur/*` is tracked in this repo, not just published to the
  AUR.** It keeps the PKGBUILDs testable and reviewable here (see Local
  testing above), and `scripts/update-bin-pkgbuild.sh` writes to the copy
  here first.
