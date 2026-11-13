<p align="center"><img src="assets/logo/stallion-logo.png" alt="Stallion" width="420"></p>

# Stallion — quick start (1.0)

**Stallion** is a **synthesizer** firmware for the M-VAVE FM-1, named after the Sea Stallion from Glendalough, the Viking longship built in Dublin around 1042. It grew out of [Clipper](https://github.com/isod89/clipper-fm1) by 3dSam (isod89), which grew out of [SLOOP](https://github.com/isod89/sloop-fm1); both are based on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita / Hügelton Instruments. Stallion is one instrument, like the original firmware, Felucca or Baudgirl's FM-1+VA. It is not a groovebox: no tracks, no drum machine, no song mode, no built-in samples. It keeps SLOOP's engines, effects, arpeggiator, step sequencer, USB audio and MIDI, and adds **HYBRID** (three oscillators, two filters, a 16-slot matrix) and many more engines. Pitch bend, mod wheel, aftertouch and the sustain pedal work on every engine. *(En français : [DEMARRAGE-RAPIDE-FR.md](DEMARRAGE-RAPIDE-FR.md).)*

> **Version 1.0.** Stallion 1.0 is built on Clipper 1.3, which runs on the FM-1, and there is still a lot to do (see [What is left to do](README.md#what-is-left-to-do)). **Before installing, make a backup** with the editor (Stallion's, Clipper's or SLOOP's, whichever is installed): the Backup tab (Projects → Backup in SLOOP or an older version) → *Save a backup*.

On the FM-1, Stallion shows its boot screen (the longship and **STALLION**, with **1.0 8V 2 CORES** under it: the version, the voices, the cores), and HOME (held) → SYS → ABOUT reads **STALLION** and **1.0 8V 2 CORES**.

## 1. Install

**Without building anything**: open the [site](https://eberonltd.github.io/stallion-fm1/) in Chrome or Edge with the instrument on USB and pick its installer: the [FM-1 installer](https://eberonltd.github.io/stallion-fm1/webapp/installer/) or the [SMK-37 Pro installer](https://eberonltd.github.io/stallion-fm1/webapp/installer-smk37/). Make a backup with the [online editor](https://eberonltd.github.io/stallion-fm1/webapp/editor/) first, then click **Install**. The firmware package is also attached to each [release](https://github.com/EberonLtd/stallion-fm1/releases).

**From the source, on Windows**:

1. Close SLOOP's window if it is still running (not required, just clearer). Stallion lives in its own folder, `STALLION`, next to SLOOP's; nothing is shared between the two (Stallion's local pages are on port **8767**, SLOOP's on 8766).
2. Double-click **`INSTALL-STALLION.bat`**. It uses the same tools as SLOOP (WSL MinUI-Build, the JieLi toolchain). The first time it copies the three SDK files from `..\SLOOP\build\deps\ac79`; if it cannot find them, copy SLOOP's `build\deps\ac79` folder into `STALLION\build\deps\`.
3. The installer opens at http://localhost:8767/webapp/installer/ (Chrome or Edge, FM-1 on USB). Click **Install**.
4. The FM-1 restarts on the **Stallion** screen. Your SLOOP user presets and settings are still there.

Elsewhere than Windows: see [BUILDING.md](BUILDING.md).

## 2. On the FM-1

| | |
| --- | --- |
| **HOME** | The synth screen: the sound's name, its family, its number in the list, the engine and a scope. The four knobs play the sound's main controls. |
| **PRESETS** | Every sound, one by one, factory and user presets. On the SAVE > PRESETS page, KNOB 2 filters the list by engine (ALL, an engine, USER). |
| **ALGORITHM** | Jumps to the next family (PAD, BASS, LEAD, PLCK, KEYS, STAB, FX…). |
| **MENU → SCREEN → BROWSE** | **PRESET** (as above) or **ALGORITHM**: ALGORITHM then picks the engine (PONY, WAVE, FM6…) and PRESETS steps through that engine's presets only, round. Favourites and user presets stay on the PRESETS page. |
| **FX, ENV, LFO, EDIT, ARP, GLO, SAVE** | A tap opens the pages; while FX or SAVE is held, the keys still play (EDIT, ARP and GLO held are sequencer layers, see below). FX pages: FX (sends), FILTER (and MODL, the filter model, its SLOPE, LINK), FILTER 2 (a second filter: MODL, CUT, RES, ROUTE SER / PAR / SPLIT), INSERT, INSERT 2, DLY, DLY 2 (the delay's MODE), REVERB (TYPE: ROOM, PLATE, HALL, SHIMMER), REVERB 2 (SHIM: the octave-up shimmer on any TYPE), CHORUS, EQ+COMP. ENV: the envelopes (ENV, ENV DEST, ENV 2, ENV 2 LOOP). LFO: LFO, LFO DEST, MOD, BEND, BEND 2, LFO 2, LFO 3, the 24-slot MATRIX and MATRIX 2, MOD SEQ, MOD SEQ 2. EDIT: the engine's pages (EDIT 1, EDIT 2), VOICE 1–3 (SPREAD on VOICE 3), MACROS, LAYER, MORPH, RANDOM, UNDO. GLO: GLOBAL (tempo, swing, metronome, fine tune), MASTER, **TUNING** (the system, its root, and **A4** from 400 to 480 Hz: 432, 415, 442...), SYSTEM (MIDI out, sync, CPU / LATE / PEAK), **MIDI** (IN CH: OMNI or 1 to 16; OUT; BEND range; LOCAL), **MIDI 2** (PROG: program changes; SEND: the knobs' CCs; BRTH: a breath controller). SAVE: PRESETS, USER (your presets), TOOLS. |
| **SEQ, SEL, EDIT, ARP, GLO** | Held, the sequencer's layers (as SLOOP 2.7.2): SEQ the 16 steps on the keys (a step held: KNOB 1 note, 2 velocity, 3 ratchet, 4 nudge, PRESETS a lock), SEL the key and the chords, EDIT erase (a key: that note out of the pattern; KNOBs SHIFT, LENGTH, TRANSPOSE; OCT- / OCT+ undo / redo), ARP note repeat (and the arpeggiator itself: ARP + F3 starts it in UP or stops it, ARP + F#3 turns HOLD on or off; KNOB 2 its MODE, KNOB 3 HOLD, KNOB 4 RATE; ARP held 1 to 3 seconds and let go, touching nothing, works as F3), GLO fill (key 9), fill bar (key 10), tap tempo (key 16), level (KNOB 1); GLO + F3 turns the keys' aftertouch (KEY AT) on or off: **K-AT** in the top line is bright while it is on. The top line also shows **ARP** while the arpeggiator runs (**ARPH** with HOLD), **USB** and the battery. Held, a layer shows after 0.35 s (at once when you use a key or knob); a tap opens their pages. |
| **SEQ tapped: NOTES** | The pattern as a piano roll. KNOB 1 walks the notes, KNOB 2 pitch, KNOB 3 length, KNOB 4 force (GHOST SOFT NORM HARD ACC), PRESETS zoom; the keys or a MIDI keyboard write the step; EDIT tapped deletes the note; **OCT+ held**: the note's sheet (SLIDE, NUDGE, FILL, MOVE, COPY, DELETE; KNOB 1 the row, KNOB 2 the value, OCT+ does it, OCT- closes). MENU > SEQ > STEP ADVANCE OFF: the cursor stays after a chord. |
| **REC** | Records what you play into the pattern. Hold REC for about 2 seconds to clear the pattern: a ring fills, and letting go before it is full clears nothing. |
| **MIDI** | GLO > **MIDI**: **IN CH** OMNI (every channel, the default) or one channel 1 to 16; **OUT**, the channel Stallion sends on; **BEND**, the pitch bend range (AUTO or 1 to 24 semitones); **LOCAL** OFF: the keys only go out (a DAW sending the track back). GLO > **MIDI 2**: **PROG** ON loads a sound on a program change; **SEND** CC: the knobs send their CCs to a DAW; **BRTH** VOL: a breath controller (CC 2) plays the volume, V+AT the aftertouch too (set the MOD page's AT to CUT: the breath opens the filter; VOICE MONO or LEGATO). Everything Stallion receives and sends: [docs/midi.md](docs/midi.md). |
| **MENU → LIGHTS** | Hard to read the buttons? **LIGHTS** lights every button's label, from 1 (faint) to 8 (bright); the selected buttons stay fully lit. KEYS lights the C keys, the white keys or all of them at the same level. The higher levels cost a little CPU (the LED scan waits longer): if 16 voices crackle on 8, step down. |

What you are playing is kept by the autosave across power cycles; save a sound you like as a **user preset** (SAVE → USER).

## 3. Playing HYBRID

- Turn **PRESETS** (or **ALGORITHM** to change family). HYBRID's sounds are spread over the families: **STALLION**, POLY PAD, STRINGS, VOX, GLASS PAD and DRONE (PAD); ACID, WOBBLE, DRIFT BASS, FOLD BASS and BOOM SUB (BASS); SYNC LEAD, RAVE SIREN, CHIP LEAD and TRANCE (LEAD); PLUCK, SAW PLUCK, FM BELL, SPACE BEL and METAL (PLCK); FM KEYS (KEYS); BRASS (STAB); RISER and INIT (FX); and THICK BASS, THICK LEAD, LADDER PAD, CLIP WIDE, DARK PAD, ACID HARD, BRIGHT BEL and SOFT SYNC.
- On **HOME** the four knobs play **CUT**, **RES**, **M1** and **M2**.
- **EDIT**: CUT, RES, FENV (offsets on both filters), **PTCH** (the factory patch), and **M1 to M4**, four macros whose effect each patch decides. On STALLION, for example, M1 opens the filter and M2 widens the supersaws.
- From a MIDI keyboard: pitch bend, mod wheel (vibrato on several patches), aftertouch, sustain, and M1 to M4 on CC 16 to 19. On every other engine, CC 16 to 19 play the four MACROS (BRITE, TIMBR, MOTN, SPACE).
- From a controller or a DAW: **program changes** (and bank select, CC 0) pick the sounds in the list's order; **CC 11** is the expression (the volume); **CC 120 / 123** (panic) stop every note, **CC 121** resets the controllers; **RPN 0** sets the bend range; CC 7 level, 10 pan, 71..75 resonance and envelope, 74 cutoff, 76..78 the LFO, 91 / 93 / 94 the sends. The list: [docs/midi.md](docs/midi.md).

## 4. The web editor

Open the [online editor](https://eberonltd.github.io/stallion-fm1/webapp/editor/) (or double-click `OPEN-EDITOR.bat`: http://localhost:8767/webapp/editor/), click **Connect**, then pick a sound. In the **Sound** tab, the **HYBRID patch** panel shows everything: oscillators, filters and routing, ENV2 and ENV3, LFO2 and LFO3 (tempo-synced if you want), the 16-slot matrix and the bend. With **live** ticked, every change is heard at once. **Export patch** and **Import patch** keep your sounds in `.stallion.json` files (Clipper's `.clipper.json` load too).

Every card folds: click its heading to fold it, Shift+click to open that one alone and fold the rest; **Fold all** / **Open all** on the Sound tab (the browser remembers them). The patch bay, with its **MATRIX** tab as a table, is the one place for the matrix. The ENV and ENV 2 pictures show where the newest note you play on the FM-1 is; the LFO picture has RATE, PHASE and FADE to drag, a button per wave and the same playhead. The **Settings** tab shows the FM-1's pages as blocks, each with its place on the FM-1 (for example TOUCH 2 · GLO 9/13).

The other panels of the **Sound** tab: **FMX patch** (the six operators, the routing grid, STORE into the bank, `.stallion-fmx.json` files), **FM6** (six-operator `.syx` banks), **MOD SEQ** (the four lanes to draw), **Tuning** (the system, the root, A4 in Hz, load a `.scl` scale), **Sound tools** (undo / redo, compare, A / B, randomize, what changed, the envelope drawn). The **Library**, **Wavetables** (your own tables for WAVE) and **Backup** tabs complete it.

The **Sequencer** tab is a full step sequencer: undo / redo, a range (shift-click on the grid) for transpose, shift, reverse, double, copy / paste and chords; generators (Euclidean rhythms, random melodies in the key, 22 ready-made basslines, arpeggios and chord patterns, a pattern text, humanize); lanes to draw velocity, nudge, ratchet and level; and a pattern library (a bank in the browser, `.stallion-pattern.json` files).

To try the editor without an FM-1, open `web/editor.html?mock=1` in Chrome.

## 5. The test that helps most: both cores and the CPU load

This decides how many voices Stallion can play. The FM-1 must be connected to the computer by USB, otherwise the CPU box shows the USB state instead.

1. After installing, hold HOME (the menu) → **SYS** → **ABOUT**: it should read **2 CORES**. If it reads **1 CORE**, please report it.
2. Pick the **STALLION** sound with PRESETS, go to **GLO → SYSTEM**: the fourth box cycles through **CPU**, **LATE** and **PEAK**.
3. Hold **8 notes** and note CPU and PEAK. Then MENU → SYS → **2ND CORE OFF** and measure again: the difference is what the second core brings. Set it back **ON**.
4. Do the same with **POLY PAD**, **TRANCE**, an **FMX** sound (FMX EP) and **ENSEMBLE** (STRINGS).
5. Then with 1.0's features: **SPREAD** at 127 (VOICE 3), the reverb on **SHIMMER** (REVERB → TYPE) and the delay on **TAPE** (DLY 2 → MODE), 8 notes. Turn **BRITE** and **SPACE** (EDIT → MACROS) while a chord is held, and change TYPE and MODE: no click should be heard.
6. Save a user preset with some 1.0 settings, switch the FM-1 off and on, and load it again: the settings should be there.
7. If PEAK stays under ~70 % and LATE does not climb, try a 12-voice build (**`INSTALL-STALLION.bat 12`**) with 12 notes, then 16.

Please report the numbers (with and without 2ND CORE) in the [Issues](https://github.com/EberonLtd/stallion-fm1/issues), and anything odd: a crackling sound, a stuck note, a **FELUCCA CRASH** screen (photograph it: it stays 10 s), a strange screen.

## macOS: the port is called Clipper?

If the FM-1's MIDI port shows as **Clipper** or **Hügelton Instruments - Clipper** (macOS remembers the name an older firmware gave it): unplug the FM-1, open **Audio MIDI Setup** → **MIDI Studio**, delete that device (greyed out), and plug the FM-1 in again. It comes back as Stallion.

## A noise on one side?

Some players hear a crackle or a noise on the left channel, with the reverb on and on some sounds, that the original firmware does not have. Found and fixed in Clipper 1.1: **PLATE, HALL and SHIMMER** (FX > REVERB > TYPE) read one of their left taps outside its delay line, a crackle on the left only; and with REVERB > DAMP turned down and a loud sound, ROOM overflowed into a crackle that went on after the notes. Clipper 1.1 also moved the reverb's memory. The sound Stallion computes is otherwise clean on both sides (every factory sound checked, with every effect at full). If you still hear it, these tell us where it comes from (a reply on GitHub or YouTube with the answers helps a lot):
1. FX > FX: REV at 0. Does the noise stop? FX > REVERB > TYPE ROOM, then PLATE: on both?
2. Leave HOME for a still page (GLO > TUNING): does the noise change?
3. GLO > SYSTEM, the last column (CPU, then LATE, then PEAK, in turn): does LATE go up while you hear it?
4. MENU > SYS > 2ND CORE OFF, power off and on, the same sound: still there? (Set it back ON after.)
5. Is it there with no note playing?

## Starting again from scratch: FACTORY RESET

MENU → SYS → **FACTORY RESET** (the last row: PRESETS moves to it) erases everything you saved on the keyboard: the settings (menu, GLO → TOUCH and its calibration, KEY AT / PAD AT, SLOOP's MIDI settings kept in the same place), the sound and project you were playing, the four projects, the user presets and the FM6 patch bank. OCT+ asks first, in large letters, with the seconds left; press **OCT+ again** (after a moment, within 5 seconds) to erase. Any other button, a HOME tap (PATCH on the SMK-37), a key or pad, a knob, or waiting cancels. The keyboard then restarts with the default sound and settings, as after a fresh install; the factory presets are still there.

It keeps your **user samples and wavetables** (erase those slot by slot from the editor), the **HARDWARE CALIBRATION** (the learned button and knob table: teaching it again needs OCT- + OCT+ held at power-on), the keyboard's factory calibration and the firmware itself. Back up from the editor first if you might want your sounds again.

If the screen says **ERASE FAILED - TRY AGAIN**, the keyboard keeps playing (the sound and USB come back), but nothing is saved until you try again (MENU → SYS → FACTORY RESET, whose row says ERASE FAILED in red until then) or **switch off and on**. A restart by itself (an update, a crash) does not end it.

## Going back

- **To SLOOP**: run `INSTALL-SLOOP.bat` from SLOOP's folder. A sound set to HYBRID plays another engine there.
- **To the official firmware**: use the installer page ("Return to the official firmware", with M-VAVE's official FM-1.fwsc: V16, or V15), after making a backup.

## Where it comes from

Stallion is EberonLtd's firmware ([EberonLtd/stallion-fm1](https://github.com/EberonLtd/stallion-fm1)), free software under the GPL-3.0. It grew out of [Clipper](https://github.com/isod89/clipper-fm1) by 3dSam ([isod89](https://github.com/isod89)), which grew out of [SLOOP](https://github.com/isod89/sloop-fm1); both are based on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (@kurogedelic) / [Hügelton Instruments](https://hugelton.com). Every credit and licence: [README.md](README.md#credits), [LICENSING.md](LICENSING.md).
