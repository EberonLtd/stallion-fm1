# Building Stallion

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader); the tools and tests read it by this name |
| `stallion-<version>.fwsc` | the same package under the name users get (stallion R2; the version from `firmware/src/ui.c`, or `--release X.Y`) |

## Windows (WSL)

`INSTALL-STALLION.bat` (`INSTALL-STALLION.bat 12` or `16` for more voices) runs `build-stallion.ps1`:
it builds in the WSL distribution `MinUI-Build` with the JieLi toolchain at
`/root/fm1-codex-deps/jieli/toolchain` (SLOOP's setup; edit the `build_windows.py` line of
`build-stallion.ps1` if yours differ), makes the site and opens the installer on
`http://localhost:8767/webapp/installer/` (SLOOP uses 8766). It needs Python 3 with Pillow on
Windows and the three SDK files (below) in `build/deps/ac79`: the first run copies them from a
SLOOP folder next to this one (`..\sloop-fm1`, `..\SLOOP`) or from `CLIPPER_SDK`.
`OPEN-EDITOR.bat` serves the editor of the last build on the same port.

## Prerequisites (macOS)

- Python 3 with Pillow: `pip3 install Pillow`
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

Stallion's identity is its own number, 1042 (`STALLION_NUM` in `tools/fm1pkg_make.py`, the one place it is set),
then two digits: `FM-1_104200` for a development build, `FM-1_1042XY` for release X.Y. The update loader answers
`ota-` + the package's identity. (Clipper's scheme, `FM-1_9XY`, is Clipper's. Stallion builds before the change
reported `FM-1_900` / `SMK-37 Pro_900`: the installers still update them and resume their loader.)

`./build.sh --release 1.0` makes a release build: the package identity becomes
`FM-1_104210` and the version string `STALLION 1.0`; the package is `build/felucca-1.0.fwsc` (and
`build/stallion-1.0.fwsc`). A suffix marks a pre-release only when you give one: `--release 1.0-beta` reads
`STALLION 1.0-BETA`.

Build options (environment, `0` or `1`; defaults in `firmware/src/felucca.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console |
| `FELUCCA_UAC` | 1 | USB audio input: the master output, 44.1 kHz stereo (after Felucca 1.0) |
| `FELUCCA_UART` | 1 (SMK-37 Pro: 0) | TRS MIDI IN (the 3.5 mm jack); must stay 0 on the SMK-37 Pro (PH8 is a key row there) |

Target board: `BOARD=smk37 ./build.sh` (or `./build.sh --board smk37`) builds for the M-Vave SMK-37 Pro:
`-DBOARD_SMK37=1` for the app and the update loader, identity `SMK-37 Pro_104200` (release `SMK-37 Pro_1042XY`),
the SMK flash layout (PRCT 0x9D000, the stock region table with USRTRIM / USRFLASH) and the package
`build/stallion-smk37.fwsc`. The intermediate files (`build/felucca.bin`, `build/loader/ota.bin`, `build/felucca.dis`)
are shared: build the FM-1 again before `tests/run_tests.sh`. The default stays the FM-1, byte for byte.

## Samples

The synth (`CLIPPER_SYNTH 1`, the default; the name is Clipper's, kept for merging its releases) has no built-in samples: SAMPLE and GRAIN
play the four user slots sent from the editor, and the flash goes to synthesis. The CC0
instrument and drum samples in `assets/samples-cc0/` (Versilian Studios VSCO-2 CE and VCSL,
Sonic Pi for SCRCH: see `ATTRIBUTION.txt` there) are only built into the groovebox
(`CLIPPER_SYNTH 0`, which the tests use).

## Tests

```
tests/run_tests.sh
```

Runs the host tests (flash storage, user presets, MIDI parser, update entry, update
loader, every synth engine and page, the modulation matrix, the MOD SEQ, the filter models,
VINTAGE, the tunings, the arpeggiator, both cores, a click test of the whole mix, project
formats, the groovebox's 4-track mix, the regression suite, the command-line installer)
and, with Node.js, the web page tests. `HOST_ONLY=1` skips what needs the JieLi toolchain. Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build).

The regression suite (`tests/regress.c`, built as the groovebox, `CLIPPER_SYNTH 0`) renders
each of its 13 engines and their presets and compares a hash of each render with
`tests/golden.txt` (the synth's own engines and presets have their own tests: `synth*_test`,
`hybrid_test`, `wave_test`, `clean_test`...); it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). The host CPU cost is relative
to the idle + drums mix, counted by the kernel on macOS and under callgrind on Linux when
valgrind is installed (exact, about 45 s more); without either it is timed, a rough check.
After an intended change of the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites
the hashes; `BUDGET_UPDATE=1` does the same for the cost files.

Before each commit, the licence and private-files checks (Python 3, no packages; CI runs them too, with the host
tests under `HOST_ONLY=1` and the web tests, `.github/workflows/ci.yml`). Install the hook once per clone:

```
git config core.hooksPath tools/hooks
```

`tools/licence_check.py` fails on a source file without an allowed `SPDX-License-Identifier` header and on a data
file missing from `assets/MANIFEST`; `tools/private_check.py --staged` fails on an added audio file or a file in a private
rig, refs or scores folder. The release checklist is `docs/RELEASING.md`.

## Install

On Windows, `INSTALL-STALLION.bat` builds and opens the web installer (Chrome or Edge). The
`.fwsc` of each release is on the GitHub releases page.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/felucca.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/felucca.fwsc dev /tmp/felucca-site
cd /tmp/felucca-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

## Publishing a release on GitHub

Stallion's site is GitHub Pages at `https://eberonltd.github.io/stallion-fm1/`: a front page that asks which
instrument (FM-1 or SMK-37 Pro), the FM-1 installer (`webapp/installer/`), the SMK-37 installer
(`webapp/installer-smk37/`, one package per model: SMK-37 Pro, Elite, MKE-P37) and the editor (`webapp/editor/`).
It is built and published from a Mac:

```
tools/publish_github.sh                    # builds the SMK-37 packages, then the FM-1's, the site, a local gh-pages commit
../clipper-fm1/.venv/bin/python -m http.server 8767 --bind 127.0.0.1 --directory build/stallion-site
# open http://127.0.0.1:8767/ in Chrome or Edge
tools/publish_github.sh --no-build --push  # shows what it pushes, asks you to type "push", pushes gh-pages only
```

Nothing leaves the computer without `--push` and your typed confirmation, and nothing is ever forced. The script's
options (`--release X.Y`, packages given by path, `--no-commit`) are in its header and in `docs/RELEASING.md`
section 8. The release itself (tag, notes, the packages attached) follows `docs/RELEASING.md`.

`PUBLISH-GITHUB.bat` / `tools/publish_github.ps1` are Clipper 1.3's Windows tool, kept for the record and superseded:
their names, bundle and notes are Clipper's, and they force-push `gh-pages`. Do not use them for Stallion.
