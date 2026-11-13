# Releasing Stallion

The checklist before each public release (a tag, a GitHub release, the installer page). Stallion R56: nothing in
the repo or the package may expose a user to a legal claim, so a release goes out only when every box below is
ticked. Copy the list into the release's pull request and tick it there.

## 1. The tree

- [ ] `main` is up to date and CI (`.github/workflows/ci.yml`) is green on the commit to be tagged: the licence
      audit, the private-files check, the host tests and the web tests.
- [ ] The full suite with a target build passes on macOS with the JieLi toolchain (CI cannot run it: the
      toolchain is proprietary): `./build.sh`, then `tests/run_tests.sh` (it adds the M-UPGRADE entry, the update
      loader and the target cost of the render loops, which CI's `HOST_ONLY=1` skips; and the host CPU budget is
      counted there, where CI only times it). Note the build's flash, RAM
      and pool lines in the release notes' pull request.

## 2. Licence audit (R52, R53, R54)

- [ ] `python3 tools/licence_check.py` prints `ok`: every source file has an `SPDX-License-Identifier` header on
      the allowed list, and every third-party source file is named in `LICENSING.md`.
- [ ] `LICENSING.md` has one row per third-party source taken in since the last release: its licence, its author,
      what was taken, where it lives, and a link to its review (R54: the pull request that brought it in).
- [ ] Every licence text a third-party file needs is in `LICENSES/` or next to the file, unchanged.
- [ ] Code inherited from Clipper that was ported from other projects (R53: OberVave, TripDeck, CS-1, the second
      core's HAL, CrispyZebra) is still listed, and the section 7 question R53 raises is settled (R51 holds:
      `LICENSING.md` "No additional permission").

## 3. Asset manifest (R55)

- [ ] `python3 tools/licence_check.py` lists no data file missing from `assets/MANIFEST`, no row with a licence
      outside R11's list and no row for a file that is gone.
- [ ] Every file of the shipped package that is not built from this tree is accounted for: the firmware image's
      generated data (`build/gen/*.h`, GPL-3.0-only, made by `tools/gen_*.py`), the three AC79 SDK files the package
      carries (Apache-2.0, `LICENSING.md`), the installer and editor pages (`web/`, GPL-3.0-only; the Fukiai font, MIT).
- [ ] Each CC BY file's credit line is in `assets/MANIFEST` and in the credits the package and the editor show.
- [ ] No asset is still marked "to be replaced" (R1: Clipper's logo, screens and web app icons) unless the release
      notes say so.

## 4. Trademarks (R6)

- [ ] The deny-list test (`tests/denylist.txt`, in `tests/run_tests.sh`) passes: no engine, preset, parameter,
      enum or editor label holds a word of the list.
- [ ] The release notes, the installer page and the README name no brand or model in a way that suggests
      affiliation (the FM-1 is named only as the hardware Stallion runs on; `LICENSING.md` "Trademarks").

## 5. Private material stays out (R12, R13)

- [ ] `python3 tools/private_check.py --tree` prints `ok`, and `--base <last release tag>` too: no audio file
      added, no recordings, reference material or note scores.
- [ ] Every line added to `tools/approved_audio.txt` since the last release (the only audio content the check
      lets in, by git blob id) was reviewed by a maintainer, and its file has its `assets/MANIFEST` row.
- [ ] The release builds and its tests pass from a clean clone of this repo alone (CI does it on every push).

## 6. Preset levels

`tests/run_tests.sh` measures every factory sound of the synth (about 25 s) and fails when one is more than 1.5 dB
off its level target. When a sound, an engine or the mix changes and it fails, re-level, then commit the new
`firmware/src/preset_trim_clipper.h` and the golden renders with the change:

    cc -O2 -w -DCLIPPER_SYNTH=1 -Ibuild/gen -Ifirmware/src -Ifirmware/hal tests/preset_level.c -lm -o build/host/preset_level
    build/host/preset_level build/host/presets.wav build/host/presets.txt build/host/presets.raw build/host/presets.peak
    .venv/bin/python tools/level_presets.py build/host/presets.raw build/host/presets.txt clipper peaks=build/host/presets.peak

Repeat the last two commands until nothing moves (two or three rounds; the trims add up). A new preset needs its
phrase letter in `tests/preset_level.c` `ROLES` first. The loudness is BS.1770 with half of the ISO 226 low-end
correction, measured before the master limiter with MASTER all the way up. A boost stops where the sound's peak
reaches +6 dBFS there. The check lists the short, peaky sounds that stop at that bound; it does not fail them.

## 7. Release notes

- [ ] `docs/release-X.Y.md` states, in these words or better:

      > Every part of Stallion is free to use, change and share under its listed licence: the code under
      > GPL-3.0-only (the few third-party source files under the GPL-compatible licences listed in
      > `LICENSING.md`), the data (samples, fonts, images) under the licences listed file by file in
      > `assets/MANIFEST`. Nothing in Stallion is "all rights reserved".

- [ ] The notes credit Clipper (isod89), SLOOP and Felucca (Leo Kuroshita / Hügelton Instruments) and the
      CC BY authors (`assets/MANIFEST`), and link `LICENSING.md`.
- [ ] **README, at the top** (before the Clipper history), three sections kept current at every release (owner,
      2026-11-08):
  - [ ] **Supported hardware:**
    - the M-VAVE FM-1, both flash chips (Puya P25Q80 `0x856014` and UC25HQ80 `0xB36014`; say which is
      confirmed on real units);
    - the M-VAVE SMK-37 Pro (37 keys, pads, wheels, pedal jack);
    - one package per model, and how to tell which to install.
  - [ ] **New features in Stallion:** everything Stallion adds over upstream Clipper, grouped:
    - engines;
    - the matrix (triggers, CC sources, sends);
    - performance (SMK-37 pad and key pressure, the FM-1's KEY TOUCH, the bend lever and ribbon);
    - the web editor (patch bay);
    - the installer (the longship sailing along the web page's progress bar; the instrument shows the logo and a
      thick bar).

    Each with one line on what it does musically.
  - [ ] **Bug fixes:** what Stallion fixed in code it took from upstream. Mark each one **offered upstream** (with
    the PR link) or **Stallion only**, so upstream authors can take them.
- [ ] Every item in those sections comes from the merged PRs since the last release (`git log` of `main`), not
      from memory; an item without a PR is checked in the code.
- [ ] **Credits:** a CREDITS page on the device (ABOUT) and in the README listing Stallion and everyone it builds
      on. **Additional terms (GPL-3.0 §7):**
  - (b) author attributions in Stallion-written files;
  - (e) no use of the Stallion name or logo for modified versions.

  They are drafted in `LICENSING.md` for review. They apply only to files Stallion wrote.
- [ ] **Copyright notices** (owner, 2026-11-08):
  - Every Stallion-written file carries `Copyright (C) 2026 Eberon Limited (John Gorman)`.
  - The six files that say `EberonLtd (Stallion)` are changed to it, and so is `LICENSING.md` line 11.
  - Files Stallion changed but did not write keep their authors' lines and gain
    `Changes: Copyright (C) 2026 Eberon Limited (John Gorman)`.
  - Upstream notices are never removed or altered (GPL-3.0 §4, §5).
  - Which files are Stallion-written is decided from git history (created after the fork from Clipper), not by
    guessing.

## 8. GitHub, at the first public release

While the repo is private on GitHub's free plan, these are not available; they come with going public (or GitHub
Team). Done once, then checked at each release:

- [ ] Branch protection on `main`: pull requests required, the CI checks required (`licence audit, private
      files`, `host tests`, `web tests`), and review by the code owners (`.github/CODEOWNERS`: the maintainer's
      GitHub account named there; an organisation is ignored there). No force-push, no deletion.
- [ ] GitHub Pages from the `gh-pages` branch: the installer and the editor at
      `https://eberonltd.github.io/stallion-fm1/`, the URL the pages and README link. Built and published from a Mac
      by `tools/publish_github.sh` (below). Settings > Pages: Deploy from a branch, `gh-pages`, `/ (root)`; HTTPS
      enforced (github.io has it; a custom domain would need its DNS and certificate first). Open the installer and
      the editor in Chrome with an FM-1 connected before announcing.
- [ ] The repo's description, topics and social preview use Stallion's own name and images (`assets/logo/`).
- [ ] CI's Actions minutes: on a public repo they are free; the host tests may then run on every push again
      (`.github/workflows/ci.yml` `on:`).

### The site, from a Mac: `tools/publish_github.sh`

It replaces `tools/publish_github.ps1` (Windows, Clipper 1.3's names and bundle; kept only for the record, see
its header) for the site. Python is `../clipper-fm1/.venv/bin/python` (`PYTHON=` overrides it), never a global
one.

1. `tools/publish_github.sh` builds the SMK-37 Pro package (Stallion supports the SMK-37 Pro only),
   then the FM-1's (last, so `build/gen` is the FM-1's: `web/make_site.py` reads the installer's counts from it, and
   every board writes `build/stallion-X.Y.fwsc`). It keeps them in `build/publish/`, runs `web/make_site.py` into
   `build/stallion-site` and commits that folder to the **local** branch `gh-pages` (git plumbing with a temporary
   index: the checkout and the current branch are not touched; the parent is the remote's `gh-pages` when it has
   one, so the push is a fast-forward, never forced; no commit when nothing changed). Nothing is pushed.
   Options: `--release X.Y` (release identities), `--version X.Y` (what the site states), `--fm1 PATH` /
   `--smk37 PATH` (the SMK-37 Pro's; Stallion supports the SMK-37 Pro only) / `--no-build` (packages already built),
   `--no-commit` (the site only).
2. Look at it locally: `../clipper-fm1/.venv/bin/python -m http.server 8767 --bind 127.0.0.1 --directory
   build/stallion-site`, then `http://127.0.0.1:8767/` (the front page), `/webapp/installer/`,
   `/whats-new.html`, `/webapp/installer-smk37/` and `/webapp/editor/` in Chrome or Edge
   (Web MIDI needs https or localhost; Safari and iPhone / iPad have no Web MIDI). The site uses relative links
   only, so it works the same at the root and under `/stallion-fm1/`.
3. `tools/publish_github.sh --no-build --push`: prints the remote, the commit, its author and what it changes,
   then pushes `gh-pages` alone (not `main`, no tags) only after you type `push`. The commit message is plain and
   carries no tool attribution.
4. The device screenshots on the installer (`web/screens/`, copied to `webapp/screens/`) come from the host
   harness: `../clipper-fm1/.venv/bin/python tools/make_screens.py` after an FM-1 build, when a screen changes.

The site holds both instruments. `web/make_site.py FM1.fwsc X.Y OUT --smk37 SMK.fwsc [--smk37 ELITE.fwsc
--smk37 MKE.fwsc]` writes the front page (`web/index_site.html`: FM-1 or SMK-37 Pro, in large text), the FM-1
installer (`web/index_pkg.html` with `fm1ota.js`) and the SMK-37 installer (`web/index_smk37.html` with genkigenki's
`smkota.js` / `smkpkg.js`, at `webapp/installer-smk37/`), both with the same chooser at the top, and lists both in
`sw.js`. It refuses an SMK-37 package as the FM-1's, an FM-1 package with `--smk37`, and a site without the SMK-37
Pro package. `node web/test_pro.mjs --stallion` tests the SMK-37 page end to end.

## The checks on every commit

Install the pre-commit hook once per clone, from the repo root: `git config core.hooksPath tools/hooks`. It runs
`tools/private_check.py --staged` and `tools/licence_check.py` (Python 3, no packages), the same checks as CI.
A problem it reports is fixed, never skipped: add the SPDX header (`GPL-3.0-only` for Stallion's own files; a
third-party file keeps its own licence and gets a `LICENSING.md` row), add the data file's `assets/MANIFEST` row,
or take the audio or private file out of the commit (`git restore --staged <file>`). A file of unknown origin is not
committed until its origin and licence are known.
