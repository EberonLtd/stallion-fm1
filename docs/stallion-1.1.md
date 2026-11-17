# Stallion 1.1

Stallion 1.1 fixes and refines Stallion 1.0, free, open-source synthesizer firmware for the **M-VAVE FM-1** and the **M-VAVE SMK-37 Pro**, by Eberon Limited (John Gorman).

Stallion is built on [Clipper](https://github.com/isod89/clipper-fm1) by 3dSam (isod89) and Clipper Pro, its SMK-37 port (isod89 with M. Geisler); Clipper grew out of [SLOOP](https://github.com/isod89/sloop-fm1), and all of them are based on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita / Hügelton Instruments. Everyone it builds on is named in [CREDITS.md](../CREDITS.md), with every licence in [LICENSING.md](../LICENSING.md).

## Licence

Every part of Stallion is free to use, change and share under its listed licence: the code under GPL-3.0-only (the few third-party source files under the GPL-compatible licences listed in `LICENSING.md`), the data (samples, fonts, images) under the licences listed file by file in `assets/MANIFEST`. Nothing in Stallion is "all rights reserved".

## Install

- Open [eberonltd.github.io/stallion-fm1](https://eberonltd.github.io/stallion-fm1/) in Chrome or Edge on a computer, or Chrome on Android, and press **Install on FM-1** or **Install on SMK-37 Pro**.
- Packages: `stallion-1.1.fwsc` for the FM-1, `stallion-smk37-1.1.fwsc` for the SMK-37 Pro. They are built and signed on GitHub from this release's tag; check one with `gh attestation verify stallion-1.1.fwsc --repo EberonLtd/stallion-fm1 --signer-workflow zvenson/fm1-build/.github/workflows/build.yml`.
- Identity: FM-1_104211 and SMK-37 Pro_104211.
- Your sounds, settings and projects stay as they are. Save a backup with the editor first all the same.

## What is new

- **Quiet sounds at a low volume no longer buzz.** The output now keeps the extra resolution of the 24-bit converter through the volume, the limiter and the soft knee, so a very quiet sound with the volume down stays smooth instead of turning into a buzzing staircase. At full volume the output is exactly as before.
- **The macros glide.** BRITE, TIMBR, MOTN and SPACE move to a new value over about 40 ms instead of jumping, so a fast fader sweep no longer steps or wobbles. A newly loaded sound starts at its values at once.
- **SMK-37 Pro faders follow faster.** A fader move reaches the sound within a few milliseconds, also while the screen is redrawing.
- **Gentler aftertouch on the filter.** AT> CUT opens the filter up to about 2 octaves instead of 4, on every engine, so held pads stay warm.
- **A softer key swell on the SMK-37 Pro.** GLO > KEY AT's CEIL now defaults to 64 (half pressure) instead of 127. A unit keeps the CEIL it has saved; set it in GLO > KEY AT.
- **PONY's factory sounds re-voiced.** 18 of Clipper's PONY sounds, brighter than the instrument they follow, have a lower cutoff (and some sub on the strings), measured against recordings of the original playing pads and chords; levels re-matched.
- **Releases are built and signed on GitHub** with the FM-1 Firmware Hub's shared build workflow.

## Fixes

- Fitting the new code: the drum table's start-up image moved out of the flash, and the FM-1's fast RAM code area grew, so both boards keep a margin.
- The README, the site and the 1.0 notes credit Clipper and OberVave where Stallion builds on them, and no longer list a fix Clipper made first.
