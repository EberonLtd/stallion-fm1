<p align="center"><picture><source media="(prefers-color-scheme: light)" srcset="assets/logo/stallion-logo-light.png"><img src="assets/logo/stallion-logo.png" alt="Stallion" width="460"></picture></p>

<p align="center"><b>A deep synthesizer firmware for the M-VAVE FM-1 and SMK-37 Pro.</b><br>
Free and open source (GPL-3.0), by Eberon Limited (John Gorman). It grew out of <a href="https://github.com/isod89/clipper-fm1">Clipper</a> by 3dSam (isod89), which grew out of <a href="https://github.com/isod89/sloop-fm1">SLOOP</a>; both are based on <a href="https://github.com/hugelton/Felucca">Felucca</a> by Leo Kuroshita / Hügelton Instruments.</p>

<p align="center"><b><a href="https://eberonltd.github.io/stallion-fm1/">Install it from the browser</a></b> · <a href="https://eberonltd.github.io/stallion-fm1/webapp/editor/">Editor</a> · <a href="https://github.com/EberonLtd/stallion-fm1/releases">Releases</a> · <a href="QUICKSTART.md">Quick start</a> · <a href="DEMARRAGE-RAPIDE-FR.md">Démarrage rapide (français)</a> · <a href="BUILDING.md">Building</a> · <a href="web/EDITOR_PROTOCOL.md">Editor protocol</a> · <a href="docs/sloop/">SLOOP's manuals</a></p>

---

> **Status: Stallion 1.0.** Stallion is built on Clipper 1.3, which runs on the FM-1, and it passes its whole test suite on a computer. What Stallion adds is below ([New in Stallion 1.0](#new-in-stallion-10-more-insert-types)); the sections headed *Clipper X.Y* are Clipper's own release notes, kept because Stallion has all of it. There is still a lot to do (see [What is left to do](#what-is-left-to-do)). Install it at your own risk, and keep a backup made with the editor. You can go back to the official firmware (V16 or V15) from the installer page, to Clipper from its own installer page, or to SLOOP with its own `INSTALL-SLOOP.bat`.

**The name.** Stallion is named after the *Sea Stallion from Glendalough* (*Havhesten fra Glendalough*), the Viking longship built in Dublin around 1042 from Wicklow oak. Its logo, drawn from code by `tools/gen_logo.py`, is a longship whose stem rises into a horse's head, under a striped square sail; on the FM-1 it is the boot screen, with the version, the voices and the cores under it (for example **STALLION · 1.0 8V 2 CORES**).

SLOOP is a groovebox. **Stallion is a synthesizer** with 26 engines and 819 sounds, one instrument (two in MULTI) like the original FM-1 firmware, Felucca and Baudgirl's FM-1+VA. It has no tracks, no drum machine, no song mode and no built-in samples. It keeps SLOOP's sound engines (all but LOFI), effects, arpeggiator, step sequencer, USB audio and MIDI, and Clipper's **HYBRID**: one voice built from three oscillators, two filters and a modulation matrix, with the whole patch editable in the browser.

## Supported hardware

| Device | Package | Notes |
| --- | --- | --- |
| **M-VAVE FM-1** | `stallion-<version>.fwsc` | Both flash chips: the Puya **P25Q80** (JEDEC ID `0x856014`), in the units Felucca, SLOOP and Clipper were made on, and the **UC25HQ80** (`0xB36014`), in units sold from late 2026 (mostly on AliExpress). The app, the USB rescue and the update loader know both (from Clipper 1.3.1, `firmware/hal/fm1_flash_id.h`); the recovery tests check both and refuse any other chip. Confirmed on real units: the P25Q80 (every FM-1 Felucca, SLOOP and Clipper ran on before late 2026). The UC25HQ80: upstream's support, checked here by the tests; not yet confirmed by Stallion on a UC25HQ80 unit. |
| **M-VAVE SMK-37 Pro** | `stallion-smk37-<version>.fwsc` | The 37-key keyboard with pads, pitch and mod wheels and a pedal jack, from Clipper Pro's port (merged as history). Stallion's SMK-37 builds have been flashed and played on an SMK-37 Pro. |

**Which package?** One package per model: the FM-1 is the small box with its own speaker and 27 keys; the SMK-37 Pro is the 37-key keyboard. The installer reads which board a package was built for, from the package itself, and refuses or warns before it writes anything; on the SMK-37 the keyboard's factory calibration is never written.

## New features in Stallion

Everything Stallion 1.0 adds to [Clipper](https://github.com/isod89/clipper-fm1) by 3dSam (isod89) and Clipper Pro, its SMK-37 port (isod89 with M. Geisler), which Stallion is built on; Clipper grew out of [SLOOP](https://github.com/isod89/sloop-fm1), and all of them are based on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita / Hügelton Instruments. Stallion keeps everything they do; this list is only what it adds. Every item was checked against the code.

**133 new features and fixes:** 26 engines and 819 factory sounds, 4 of the engines new (PALOMINO, STAMPEDE, RODEO, MARE) and PONY rebuilt from the ground up (25 engine and sound features) · 6 filter features · 2 effect features · 12 modulation and matrix features · 10 playing features · 11 touch and aftertouch features · 8 sequencer and arpeggiator features · 9 screen and control features · 14 web editor features · 5 saving features · 3 MIDI features · 2 installing features · 26 fixes.

**819 sounds on 26 engines.** Every engine now comes with a full bank of its classic sounds.

- **A full bank for every engine**: 32 classic sounds on 23 engines, 28 on ACID and SAMPLE, 27 on WSEQ (819 in all), every bank starting with a plain INIT.
- **Centred and level-matched**: every factory sound has its left and right within 1 dB, and each is measured with a phrase that suits it and trimmed to the same loudness (within 1.5 dB; the sample-based SAMPLE and GRAIN sounds keep their own levels).
- **Synth sounds for a keys player**: pads, leads, basses, synth brass and strings, keys, organs, plucks and sequences. No bells, chimes, mallets, drums or imitations of other players' instruments.
- **Sounds that move**: the PONY, PALOMINO, STAMPEDE, RODEO and MARE sounds, and every sound new in this release on the other engines, are patched in the matrix, so LFOs, envelopes, velocity, pressure and the wheel move them as you play. A factory sound can use all 24 matrix slots.
- **MARE, from the instrument's patch books**: 32 sounds built from the patch books' routings and techniques, in balanced stereo, with SPACE working as the instrument's SPACING and P>VCO as its PITCH MOD ASSIGN.

**PALOMINO, the whole story.** PALOMINO follows the later, smaller edition of its instrument, and Stallion carries over what that edition added.

- **Six waves on both oscillators**: sine and noise on VCO 1, triangle and square on VCO 2.
- **VCO-2 LOW**: VCO 2 drops eight octaves to become a slow modulator.
- **A choice of where the modulation goes**: P>VCO sends the LFO and envelope pitch modulation to both oscillators or just one.
- **Five new effects**: PITCH, PIT2, PHAS6, DELAY and LOCMP on every sound's INSERT.
- **Envelopes that follow the key**: ERATE, so high notes are shorter.
- **A second filter and steeper slopes**: FILTER 2 and SLOPE 12, 24 or 48 dB, on every sound.
- **32 sounds**: pads, strings, brass, leads, basses and keys.
- **Even unison**: fixed phase offsets, so a stack is as loud every time.

### Sound engines (25)

- **PALOMINO**: a big early-1980s 8-voice poly. Two VCOs with six waves each, cross mod, hard sync, an OTA filter at 12 or 24 dB. 32 sounds: pads, strings, brass, leads, basses and keys.
- **STAMPEDE**: a late-1970s poly with two complete channels per voice. A wave morph through noise, saw, pulse, sine and ring; channel II's feet and detune; a resonant high-pass then low-pass with its own envelope. 32 sounds.
- **RODEO**: the punchy, brassy 8-voice polys of 1979-84, ported from zvenson's OberVave. Two oscillators with hard sync and X-MOD, and a filter in six modes (LP12, HP12, BP12, NOTCH, LP24, MORPH). RODEO's sounds do not follow the key with their filter, like the originals (measured on the references); **KYBD** (EDIT 3) adds tracking if you want it, from 0 (the default) to 100 % (one octave of cutoff for each octave played; 86 % is how RODEO played before). **PW0** (EDIT 3) sets the width the pulse rests at: 46.8 % on the sounds after the earlier single-voice module these polys grew from (BP SWEEP PAD, SOLO LEAD, PULSE LEAD, SYNTH BASS, SVF BASS and MORPH PLUCK), 49.5 % on the others and new sounds (53 %, RODEO's earlier width, is there too). At the top of its range (RES 124 to 127) the resonance sings a steady sine at the cutoff, as the originals do at full resonance, in every filter mode; below that it rings and dies away as before. 32 sounds.
- **MARE**: a semi-modular paraphonic voice with up to four VCOs and two ladder filters; its stacked, gliding sounds play best in PARA, where each held key takes one VCO. The filters sit apart in stereo, side by side (high-pass and low-pass) or in series. 32 sounds: basses, leads, pads, sequences, drones, keys, strings and brass.
- **PALOMINO's extra waves**: sine and noise on VCO 1, triangle and square on VCO 2, as on the modern reissue of the instrument it follows.
- **PALOMINO VCO-2 LOW**: VCO 2 drops eight octaves to become a slow modulator for wobbles and slow vibrato.
- **P>VCO** on PALOMINO and MARE (MOD page): send the LFO and envelope pitch modulation to both VCOs or just one, for sync sweeps and beating. On MARE it is the instrument's PITCH MOD ASSIGN.
- **PONY's panel**: the controls of the 1980s DCO poly it follows. **RANGE** (EDIT 3) sets the oscillator to 16', 8' or 4'. **WAVE OFF** (EDIT 1) turns off the saw and the pulse, for the sub or the noise alone. **KYBD** (EDIT 3) sets how far the filter follows the keys, from 0 to 100 % (one octave of cutoff for each octave played); the default, 43 %, is PONY's old tracking. **VCA** (EDIT 3) is ENV or GATE: GATE turns the sound on and off with the key, and the envelope still moves the filter. **CHAR** (EDIT 3, in STEPS' place) chooses how much of the original instrument's quirks PONY plays with (below). **MODL** (FX > FILTER) now works on PONY: OTA, the instrument's filter, is the default, and LADDR gives PONY's earlier ladder sound back.
- **PONY's character**, measured against the original instrument: raising RES keeps the fundamental and makes the whole sound louder, as the original does, instead of thinning it (OTA). The pulse goes from a square that is very slightly off 50 % to about 11 % at PW 127. The LFO's pulse-width modulation works from the PW knob's width narrower and back, smoothly, with no flat half; the wheel, pressure and the matrix fold back at the square instead of sticking there. On PONY the mod wheel's vibrato reaches about ±4 semitones at full wheel, gently at first, like the original's lever. ENV DEST FLT at ±64 covers the whole cutoff range and moves it evenly as the envelope falls, so an inverted envelope keeps the filter shut, then opens it. The LFO delay (FADE) rises as the original's does: quickly at first, then more slowly. **The filter as an oscillator**: with WAVE OFF, SUB and NOIS at 0 and RES at 120 or more, the filter sings on its own as soon as you play a key; with KYBD at 100 % it plays in tune with the keys (within a few cents from C2 to C6) and CUT sets its tuning. PONY's sounds were re-levelled for it.
- **PONY's CHAR**: the original instrument's quirks, each a switch. **CLEAN** has none of them, **CLASSIC** (the default) the ones that match recordings of the original, **FULL** all of them; the FM-1 shows CLEAN, CLASC, FULL or CUSTM (any other mix, set in the web editor, which has a box for each). One line each, what and why:
  - **STEPS**: the cutoff knob, MIDI CC 74 and the editor move the filter in 128 audible steps, as the original reads its slider digitally (off: the cutoff glides, as steps on a knob or a DAW ride usually sound like a fault). On in RESO SWEEP, FUNK BASS and DARK PLUCK, whose hand sweep is the point.
  - **HISS** (CLASSIC): the chorus hisses softly whenever it is on, between notes too, about 63 dB under a held chord, each side its own, as the original's bucket-brigade chips do.
  - **HPF** (CLASSIC): the high-pass filters the voices together after the amp, as on the original, not each voice before its filter; position 0 is the original's bass boost, about +10 dB in the deep bass.
  - **LEVER LFO** (CLASSIC): the mod wheel's vibrato takes the LFO's rate and wave, as the original's lever does, not a separate vibrato.
  - **GATE 1 MS** (CLASSIC): VCA GATE switches in about 1 ms, as the original's, not 3 ms.
  - **DARK NOISE** (CLASSIC): the noise rolls off above about 5 kHz, as the original's noise source.
  - **VCA CURVE** (FULL): the original's amp curve, quiet envelope tails falling away fast. Not in CLASSIC: it made a key's thump much stronger than the recordings have.
  - **LFO DELAY** (FULL): the LFO runs freely and its delay waits, then fades in straight, as the original's processor does it. Not in CLASSIC: the gradual fade fits the recordings better.
  - **COARSE CUT** (FULL): the cutoff knob in the original's coarser steps, about 1.35 semitones apart.
  - **CPU TICK** (FULL): the envelopes and the LFO move every 4.2 ms, softened over about 1 ms, as the original's processor computes them: fast envelopes get a slight grain.
- **PONY's chorus, measured from the original**: I at 0.53 Hz and II at 0.87 Hz, each side's delay its own (the left a little longer), the delayed sound 1.6 times the dry and brighter-edged (about 2.8 kHz), so the chorus widens the sound and makes it about 4 dB louder, as on the original. The INSERT's chorus types (BBD1, BBD2, BBD12) are the same chorus. **I+II** is PONY's own: a fast, shallow chorus the original does not have.
- **WHISTLE**, a new PONY sound in CLAV's place: the filter whistling on its own, in tune, the wheel its vibrato.
- **MARE VCO 3 and VCO 4** (EDIT 3): two more oscillators at their own intervals. FILTER FM and RING are matrix destinations: VCO 2 sweeps both filters at audio rate, or rings against VCO 1.
- **MARE split modes** (MODE SPLT, SPL+N): VCO 1 alone goes to the left filter and VCO 2 alone to the right.
- **MARE's normals**: a cable into VCO 1's pitch moves VCO 2 too, and filter 2 follows filter 1's cutoff, until you give them cables of their own.
- **STALLION**: a new signature HYBRID sound.
- **Every factory sound level-matched and centred**: no sound jumps out louder or quieter than the next, and none sits in one ear (left and right within 1 dB). The test suite checks both.
- **PALOMINO VCO-2 fine tune** (EDIT > VOICE 2 > DTUNE): sets how far VCO 2 sits above VCO 1, from 0 to about 16 cents (the old fixed 5 cents is the default, 40), for thick brass, strings and pads.
- **Resonance that stops short of howling**: PALOMINO's filter now stops just below self-oscillation at full RES, and RODEO's up to RES 123, so a resonant bass or brass rings and decays instead of whistling. RODEO's top four steps sing on purpose, as its originals do.
- **PALOMINO pulse width one way**: SHAPE (and an LFO or envelope on it) sweeps the pulse from square towards narrow, once per cycle, instead of folding both ways.
- **An analog attack** on PALOMINO, STAMPEDE, RODEO and MARE: the attack charges quickly and then eases in, the same length as before, for swells and brass with more bite.
- **POLY2** (EDIT > VOICE 2 > ALLOC): a new note cuts the notes already let go, so only the last chord keeps its release. Good for sequences and arpeggios.
- **Free delay times** (FX > DELAY > TIME): 35, 50, 75, 100, 150, 200, 300, 400, 500, 600, 700 and 780 ms, besides the tempo values, on every sound.
- **MARE SPACE and DIST**: SPACE works as the instrument's SPACING: it moves VCF 1 alone (the left low-pass, or the high-pass in PARA and SERI), clockwise up, while VCF 2 stays at CUT. DIST drives the two ladder filters, so it thickens the sound before the filters instead of coming after them.
- **STAMPEDE plain SINE after the filters**: the SINE wave ignores the filters, as a plain sine channel should. IL can go below zero, so a note can start under the cutoffs as set.
- **819 factory sounds on 26 engines**: a full bank of classic sounds for every engine (32 on 23 engines, 28 on ACID and SAMPLE, 27 on WSEQ), each starting with a plain INIT.
- **Sounds that move**: the PONY, PALOMINO, STAMPEDE, RODEO and MARE sounds are patched in the matrix (154 of their 160 sounds, up to 7 cables each; the sounds new in this release on every other engine have 3 to 7 cables each): pads breathe, brass swells with velocity and pressure, keys and plucks answer how hard you play. A factory sound can now use all 24 matrix slots, as a user preset does.
- **MARE's bank, from the instrument's patch books**: 32 sounds built from the routings and techniques of the patch books for the instrument MARE follows, INIT the instrument with nothing patched. MARE's sounds keep the delay and reverb their patches name; every other factory sound starts dry. The pads, pedals, strings, keys and swells, and PIPE BASS, SWEEP BASS and PARA BASS, let go over about 4.5 s, BERLIN SEQ over about 10 s; the other basses, the leads, sequences and riffs, the stab, the organ and the pluck keep a short release.
- **MARE in balanced stereo**: with the two filters apart, both sides stay equally loud while their timbres differ. Before, the darker side was up to 16 dB quieter (32 dB in PARA), so the sounds had to keep SPACE at 0.

### Filters (6)

- **FILTER 2**: a second filter on every sound, using any of the nine filter models. ROUTE: SER (an LP and an HP make a band-pass), PAR (two peaks) or SPLIT (filter 1 left, filter 2 right: each voice in stereo).
- **LINK**: FILTER 2 follows filter 1's cutoff at an offset, so one knob sweeps both. Turn LINK off to set it freely.
- **SLOPE**: the 4-pole filters at 12, 24 or 48 dB. 48 dB is steep but keeps its resonance and its bass.
- **OTA filter model**: the smooth resonant low-pass of the classic DCO and VCO polys, on ANALOG, WAVE, VECTOR and FILTER 2.
- **PONY on the OTA filter**: PONY now plays through it, with its thinner resonance. Its sub-oscillator is locked to the main oscillator.
- **CUT2 and RES2** in the matrix: modulate FILTER 2 on its own.

### Effects (2)

- **SHIM on any reverb** (FX > REVERB 2): an octave-up shimmer on ROOM, PLATE or HALL, from a touch to full.
- **Five new INSERT types**: PITCH and PIT2 (pitch shifters, one or two voices), PHAS6 (a six-stage phaser), DELAY (a short delay for slapback, doubling and combs), LOCMP (a lo-fi compressor). Their code comes from TripDeck, from twochannels' phaser, and from the Airwindows Pop3 compressor law as Lunar Modulator ports it.

### Modulation and the matrix (12)

- **Triggers and gates**: new GATE and E2END sources, and new TRIG1/TRIG2 and SYNC1/SYNC2 destinations, so one modulator can restart an envelope or an LFO. E2END into TRIG2 loops a whole envelope.
- **CC A and CC B**: any MIDI CC, or the pedal, as a matrix source (GLO > MIDI 3).
- **New destinations**: the arpeggiator's rate, the delay, reverb and chorus sends, LFO 1's rate and ENV 1's amount.
- **S&H, NOISE and +V** sources. +V is a constant, for a fixed offset.
- **Bend and pressure in the matrix**: BEND, poly pressure, channel pressure and RIBBON as sources; PIT1, PIT2, XMOD and HPF as destinations.
- **The MATRIX page**: the selected cable in large type (for example LFO1 > CUT +62), coloured by signal type. Sources and destinations are in panel order, and MARE's normals are shown in words.
- **MATRIX 2 > CLEAR**: take out all 24 cables at once. UNDO brings them back.
- **31 sources and 35 destinations** in total. User presets keep every one.
- **CHAO**: a chaotic LFO wave on LFO 1, 2 and 3, for movement that never repeats.
- **ERATE**: the envelopes follow the key, so high notes are shorter, as on an acoustic instrument. Velocity can speed up the attack too.
- **24 matrix slots** on every sound, twice as many as before. The MATRIX page, user presets, projects and the editor keep all 24.
- **Only destinations that work**: the MATRIX page offers only the destinations the sound's engine uses, and a cable into one it does not use is cleared when the sound loads.

### Playing and performance (10)

- **LEVER** (FM-1): with a key held, OCT- and OCT+ bend the pitch like a lever. Choose SPRNG, HOLD or LATCH.
- **RIBBON** (FM-1): with a key held, SELECT bends like a ribbon. Choose HOLD, SPRNG or LATCH; the range is up to 24 semitones.
- **The BEND page** for each sound: up and down range, filter bend, all notes or just the newest note, and which VCOs bend.
- **PRESS BEND**: a STAMPEDE sound that bends up when you press harder.
- **Fuller unison**: every oscillator in a unison stack starts at its own phase, so stacks are wide with no hard edge. UNISON is capped at 8 voices.
- **Even unison on PALOMINO**: its unison stacks use fixed phase offsets, so a unison patch is as loud every time.
- **PARA ASSIGN** (MENU > SEQ, for MARE in PARA and PARA+): PLAYED (the default) shares the VCOs by the order the keys went down; LOW and HIGH share them by pitch, so a chord voices the same way every time. **PARA ROUND ROBIN** ON moves the VCOs on by one with each new key (PLAYED only).
- **PARA NOTE-OFF** (MENU > SEQ, for MARE in PARA and PARA+): KEEP (the default) leaves a key you let go on its VCOs, under the shared envelope, until the next key goes down, so a chord let go fades as a chord; HAND BACK gives its VCOs at once to the keys still held. The FM-1 always keeps.
- **SUSTAIN PATCH** (GLO > MIDI 3 > SUST): the pedal stops holding notes and only feeds the matrix.
- **Lighter on the processor**: the busiest audio code (the voices, the filters, a part's mix, PONY, STRINGS, ANALOG and STAMPEDE) now runs from RAM instead of from the flash.

### Keys, pads and aftertouch (11)

- **KEY TOUCH** (FM-1, GLO > TOUCH): velocity, velocity and pressure, or pressure alone from the FM-1's own keys, with no hardware change. From ESTAMPIE by Drew Robinson, after Charles Vestal's Velo and MONK-1.
- **AT mode**: pressure alone. Every note plays at once at velocity 100, and leaning on the key adds aftertouch.
- **TOUCH CAL**: calibrate your keys with a live meter. It names any key whose soft and hard presses read alike.
- **One pressure shaping for everything**: curve, offset, attack and release (GLO > TOUCH 2), for the keys and for MIDI and MPE pressure.
- **SMK-37 Pro pad aftertouch** (GLO > PAD AT): a light press plays the note and a harder squeeze is aftertouch. Set the dead zone, range and curve on a graph.
- **SMK-37 Pro key aftertouch** (GLO > KEY AT): the keys have no pressure sensor, so pressure is made from velocity, hold time and a swell, with its own graph. A held key swells up to full pressure (CEIL 127).
- **SMK-37 Pro pad velocity and aftertouch curve** (GLO > PAD VEL, AFTERTOUCH): five pad velocity responses and the pressure shaping.
- **Pressure bar**: a thin bar shows how hard you are pressing: your touch itself, before the AT curve and offset shape the sound's response.
- **KEY AT on and off** (GLO + the lowest key): turns the keys' aftertouch on or off without opening a page. K-AT shows in the top line, bright while it is on. The SMK-37 Pro's pads always have their aftertouch.
- **Pressure that glides**: the keys' and the pads' own pressure moves to each new value over at least 10 ms, so aftertouch on a resonant filter no longer zips. A longer AT attack or release works as set; pressure from MIDI and MPE is unchanged.
- **SOUNDS from all banks or one** (SMK-37 Pro): KNOB BANK + the top key (C6) switches the pads' SOUNDS between **B**, every engine's sounds and your user presets, and **A**, the current engine's sounds only. The top line shows B or A; kept with the settings.

### Sequencer and arpeggiator (8)

- **Ratchet up to x8**: a step's notes play up to 8 times in their step (it was up to 4), evenly at any swing or nudge. Hold the step and turn KNOB 3; the step shows its count, for example C4 x8. The editor's ratchet goes to x8 too.
- **FOLLOW**: the step pages turn with the playhead, so patterns longer than 16 steps stay in view.
- **MOTION lanes**: the sequencer records the bend, the ribbon and the pressure along with the notes, and plays them back.
- **NOTE PRESS**: each recorded note keeps its own pressure curve, so a recorded pressure bend moves only its own note.
- **The arp in one move**: on the FM-1, ARP + F3 starts the arp in UP (again: stops it) and ARP + F#3 turns HOLD on or off; ARP held for 1 to 3 seconds and let go, touching nothing, does what F3 does. On both instruments, with ARP held, KNOB 2 sets the MODE, KNOB 3 HOLD and KNOB 4 the RATE. An arp started this way keeps running when you change sound.
- **The SMK-37 Pro's printed arp row**: ARP + C3 to D#4 does what is printed above the keys: UP, DOWN, INCL, EXCL, RAND, ORDER, REPEAT (each starts the arp in that mode, or stops it if it is the mode playing), OCT+, LATCH, GATE+ and GATE-, TAP, SWING+ and SWING-, TEMPO+ and TEMPO-. The keys from E4 up still repeat notes.
- **INCL**: a new arp mode, up and down with the top and bottom notes played twice (UPDN, the printed EXCL, plays them once).
- **The sustain pedal holds the arpeggio**: keys let go while the pedal is down stay in the arpeggio until the pedal comes up.

### Screen and controls (9)

- **MENU > LIGHTS 1 to 8**: the button lights now go much brighter, for players who find the labels hard to read.
- **MENU > SCREEN > BROWSE**: the ALGORITHM knob jumps engine to engine, and the PRESETS knob steps through that engine's sounds only.
- **Large, coloured MATRIX header**: the selected cable fills the top line in large type, in its signal's colour.
- **Clear touched value**: with MENU > ZOOM on, the touched value is drawn above the envelope graph, never over it.
- **Lever and ribbon bar**: a bar over the header shows how far you are bending.
- **Large prompts in TOUCH CAL** (GENTLY, FIRMLY), with a live meter.
- **Stallion's own look**: a boot screen showing the version, voices and cores (for example 1.0 8V 2 CORES), and a new icon set.
- **VOLUME bar**: turn the volume (MASTER on the FM-1, K1 on the SMK-37 Pro) and the thin bar under the four boxes shows the volume in white for 2 seconds.
- **A clearer top line**: **ARP**, **ARPH** (with HOLD) or **HOLD**, **K-AT** (the keys' aftertouch, bright while on), USB and the battery stand apart. On the SMK-37 Pro the SOUNDS' B or A sits there too, and the metronome icon before the tempo is gone.

### Web editor (14)

- **Every parameter**: the editor now reaches every parameter, including FILTER 2, SLOPE, the BEND page and the FM-1's own settings (tuning, MIDI, TOUCH, LIGHTS, BROWSE).
- **Live controls**: values are sent while a control moves, not only when you let go.
- **Envelope grab points**: drag the envelope picture's attack, decay, sustain and release points with a mouse, finger or pen.
- **The patch bay**, on every engine: drag cables between 64 jacks to patch the matrix, coloured as on the device, in large, bright type. Switch to the MATRIX tab to see the same cables as a table; up to 24 cables per sound, and only the inputs the engine uses. The matrix is shown once: the old card of MATRIX knobs is gone. FILTER FM, RING, SPLT/SPL+N and the VCO1->VCO2 pitch normal are MARE's own.
- **MIDI 3 settings** (CC A, CC B, SUST) and the new INSERT types are named in the editor.
- **Tuning card**: checked against the device, and it says what the FM-1 tunes to.
- **Device banner**: the top of every page names the instrument the editor is talking to (FM-1 or SMK-37 Pro), its firmware and the sound playing: its name, engine and voice mode, or a multi sound's name, mode and parts A and B. Without an instrument it says that none is connected.
- **Folding sections**: click a heading to fold its card to the heading (Enter or Space too); Shift+click opens one and folds the rest of the tab; Fold all and Open all on the Sound tab. The browser remembers what is folded, across sounds and engines.
- **Envelope and LFO pictures that move**: ENV and ENV 2 show where the newest note played on the instrument is. LFO, LFO 2 and LFO 3 are drawn as the firmware makes them, with RATE (and on LFO, PHASE and FADE) to drag, a button for each of the six waves, and the same playhead.
- **Settings as the device's pages**: the Settings tab shows each of the instrument's pages as its own block, with its place on the instrument, for example TOUCH 2 · GLO 9/13.
- **The sound at the top of the Sound tab**: its **NAME**, its **MODE** (OFF, LAYER, SPLIT, MULTI) and, with two parts, the sounds of part A and part B side by side. Every save of the sound, to a file, in the browser or on the instrument, takes the NAME you typed.
- **Multi sounds**: a multi sound is a name, a mode, links to two single sounds (A and B) and part B's settings: split point, MIDI channel, octave, level, pan and filter. Pick A's and B's sounds from the lists; open a part's sound on its own to change the sound itself. Keep multi sounds in the browser, in files and, on the SMK-37 Pro, in its 16 slots.
- **A two-column sound browser**: MULTI, USER and the engines on the left, that entry's sounds on the right (INIT first; NEW MULTI first under MULTI). The arrow keys move within a column and between the two.
- **Work offline**: with no instrument connected, edit a sound (part A, part B and the mode) against the editor's own copy of the firmware, save it to a file and load it into the instrument later.

### Saving and presets (5)

- **Everything new is saved**: user presets and projects keep all 24 matrix slots, FILTER 2, SLOPE, the BEND page, SHIM, CHAO, P>VCO, the new matrix codes and MARE's FILTER FM and RING.
- **Backups include your touch settings**: editor backups keep GLO > TOUCH and the key calibration.
- **Older saves load safely**: a sound saved before a feature existed loads with that feature off.
- **FACTORY RESET** (MENU > SYS): erases your settings, projects, user presets and the autosave, after asking twice. It keeps your samples, wavetables and the calibration.
- **Multi sounds on the SMK-37 Pro** (SAVE > USER, M01 to M16 after U01 to U32): SAVE keeps the links to the sounds parts A and B play, the mode and part B's settings, as MULTI 01 to MULTI 16 (the editor stores your own name). LOAD finds both sounds by name and loads them, or shows MISSING and changes nothing. A multi sound never holds a copy of a sound.

### MIDI and USB (3)

- **Aftertouch out** (GLO > TOUCH > ATOUT): the keys' pressure (FM-1 and SMK-37 Pro) goes out as channel or poly aftertouch, with each note's own velocity.
- **Lever and ribbon out**: the lever goes out as pitch bend and the ribbon as CC 16. MOTION lanes play out with the sequencer.
- **CC A, CC B and SUSTAIN PATCH**: any incoming CC can modulate the sound.

### Installing and updating (2)

- **Stallion's own identity**: the 1.0 release build answers as **FM-1_104210** on the FM-1 and **SMK-37 Pro_104210** on the SMK-37 Pro, its update loader as ota- and the same, and its USB maker name is Eberon Limited. The web installers and the terminal installer know it, and still finish a write cut off in an earlier Stallion build's loader.
- **One package per device**: stallion-<version>.fwsc for the FM-1 and stallion-smk37-<version>.fwsc for the SMK-37 Pro.

### Fixes (26)

- **Reverb chop**: PLATE, HALL and SHIMMER no longer chop in the left channel.
- **Reverb pulse and flutter**: PLATE and HALL no longer pulse, and SHIMMER no longer flutters.
- **Reverb at rest**: the reverb now rests when nothing is sent to it, saving about a voice's worth of processing.
- **Chorus RATE**: turning RATE glides instead of ticking.
- **Clicks at note start and end**: gone. The amplitude envelope now takes at least 3 ms to rise and 37 ms to fall.
- **Random note levels**: a note played over a quiet tail no longer starts at a random level (ANALOG, PALOMINO, STAMPEDE, RODEO, MARE).
- **Thin single notes**: STAMPEDE and RODEO notes no longer come out thin at random, so single notes and unison stay within 8 dB.
- **VECTOR overshoot**: changing the filter model under a held chord no longer overshoots.
- **FMX names**: the PTCH and ALG names no longer show garbage.
- **macOS 15 audio**: the FM-1 now shows up as an audio input on macOS 15.
- **Official package rejected**: the installer no longer calls the official V16 package damaged.
- **KEY TOUCH notes**: a key no longer plays another key's press, and a slow key plays its own velocity.
- **Calibration kept**: TOUCH CAL's result is kept exactly as measured.
- **Pitch bend after a project load**: a project load now sends pitch bend back to centre on MIDI out.
- **Restored backups**: after a damaged or cut-off backup, a load no longer leaves stray data in empty steps.
- **Envelope covered**: the ZOOM value no longer covers the envelope's attack.
- **HOLD ends with the sound**: loading another sound, a factory sound or a user preset, turns KBD HOLD off and lets go of the notes it held. Before, HOLD stayed on, so a new sound could come up holding every key played. An arp you started yourself still carries over.
- **Mono sounds keep their attack**: a factory sound marked mono now loads as MONO, not LEGATO, so a key played over a held one starts its envelopes again instead of sliding in without its attack (on a one-voice engine such as ACID, every overlapping key lost it). LEGATO is one VOICE setting away.
- **Quiet at power-on**: the SMK-37 Pro starts at a low volume (about 10 dB under before), and the FM-1 plays at that level until you move MASTER (a knob set lower plays as set). Headphones are never blasted.
- **Nothing in one ear**: SPREAD now places the voices around the middle. A single note sits in the centre, and chords and unison stacks spread evenly. Before, a lone note could sit hard to one side.
- **Note repeat and the reverb**: turning the note repeat's rate no longer moves the reverb's PRE.
- **CC 120, all sound off**: now silences the sound at once, whatever its release (CC 123 still lets the notes release).
- **A click after the last note**: FX > FILTER at a low setting no longer clicks when the last note's release ends.
- **DIGITAL in UNISON**: a DIGITAL sound in UNISON is no longer louder than in POLY.
- **Chords when the processor is busy**: when a new chord would overload the processor, Stallion first gives up the quietest fading notes and fades them over about 6 ms instead of cutting them: no crackle and no ticks between chords.
- **MARE ticks**: the balance between MARE's two sides now slides instead of stepping, and SPACE swept through 0 waits for the two filters to meet before it joins them: no rattle or tick in one side (PEDAL SEQ, FIFTH PEDAL, SPACE DRIFT and others).

## Bug fixes

What Stallion fixed in code it took from upstream (Clipper, Clipper Pro, and the projects they credit), so their authors can take the fixes.

**Offered upstream**

- None yet. No Stallion fix has been sent upstream as a pull request so far; each one below is ready to offer.

**Stallion only**

- **PLATE, HALL, SHIMMER: the chop.** The left output read past the end of a delay line 16 times a second (two groups of output taps swapped); the taps are now wired as in the published design, every tap inside its line (in Clipper's `fx.c`; Stallion PR #1).
- **PLATE, HALL: the helicopter pulse.** The tank's moving diffusers moved fast enough to beat; they now move slower and less (PR #1).
- **SHIMMER: the flutter.** The octave line's crossfades dipped 3 dB 49 times a second; now equal-power fades (PR #1).
- **PLATE, HALL, SHIMMER at rest.** The reverb tank ran on every block with nothing sent, about the cost of a voice; it now rests and wakes at once (PR #1).
- **The chorus's RATE** ticked when changed; it now glides (PR #1).
- **VECTOR**: a filter MODL change under a held chord overshot; the duck now waits for the restarted filter (PR #3).
- **ANALOG**: a note over a quiet tail started at a random level; it now starts as from silence (PR #13).
- **The installer** called the official V16 package damaged (it checked CRCs of entries the device never reads); now only `flash.bin` and `ota.bin` are checked, as the device does (PR #1).
- **USB on macOS 15**: the FM-1 opened but gave no audio input; the audio interfaces now come before MIDI (PR #32).
- **Clicks at a note's start and end**: the amplitude envelope now takes at least 3 ms up and 37 ms down (PR #33).
- **The touched value** (MENU > ZOOM) covered the envelope's attack on the ENV page; it is now drawn above the graph (PR #23).
- **`font_test`** did not compile on macOS (PR #18).
- **KEY TOUCH** (code from ESTAMPIE): a landing could play another press's note, and a late-seating press played the wrong reading (PR #19).

## At a glance

**26 engines, 819 sounds**, a full bank of classic sounds for each engine, every one level-matched through the synth's own output chain and centred (left and right within 1 dB):

| Engine | What it is |
| --- | --- |
| **HYBRID** | Clipper's voice: three oscillators (saw, pulse, tri, sine with feedback, morph, a seven-saw supersaw, FM, hard sync, wavefolder, noise), ring, drift, drive; two filters (LP12, LP24, a ladder, BP, HP, notch, peak) in series, parallel or split; two envelopes and two tempo-synced LFOs per voice, a 16-slot matrix, four macros. |
| **FMX** | Six operators, each its own wave (10) and mode (PM, true FM, RING), any routing with an amount per connection, 40 templates, a pitch envelope; it also plays the six-operator `.syx` voices of the bank. |
| **FM6** | A 6-operator FM engine with the 32 classic algorithms; loads `.syx` banks from the editor. |
| **DIGITAL** | Four-operator FM, eight algorithms: electric pianos, FM brass and pads, basses. |
| **WAVE** | Wavetables: 16 tables of 16 frames, band-limited per octave, and your own (a WAV of single cycles, 2048 points a frame) in four user slots. |
| **WSEQ** | Wave sequencing: 16 sequences of waves, pitches and levels at the song's tempo, crossfaded. |
| **VECTOR** | Vector synthesis: four waves at the corners of a square, a joystick-style X / Y between them and paths that move by themselves. |
| **MARE** | A semi-modular paraphonic voice: two VCOs a voice whose WAVE turns from triangle through saw and square to a narrow pulse, hard sync or linear FM, noise; two 4-pole ladder filters SPACE apart, left and right (STEREO), high-pass and low-pass side by side (PARALLEL) or in series. |
| **PONY** | A classic 1980s DCO poly: DCO saw, pulse with PWM, sub, noise (WAVE OFF: the sub or the noise alone), RANGE 16', 8', 4', the four-position HPF, the OTA resonant low-pass with KYBD tracking that self-oscillates in tune (WAVE OFF and RES high: the filter alone plays as an oscillator; or any filter model: LADDR is its earlier ladder), VCA ENV or GATE, CHAR (the original's quirks: CLEAN, CLASSIC, FULL), and the stereo BBD chorus I, II (measured from the original) and I+II. |
| **PALOMINO** | A classic 8-voice polysynth of the early 1980s: two VCOs (six waves each), cross mod and hard sync, an OTA filter at 12 or 24 dB. |
| **STAMPEDE** | A classic late-1970s poly of two complete channels a voice (I and II): a wave morph through noise, saw, pulse, sine and ring, channel II's feet and detune, a resonant high-pass then low-pass swept by their own envelope (IL). |
| **RODEO** | The voice of the 8-voice polys of 1979-84: two oscillators (saw, pulse, both, noise; hard sync), X-MOD, a state-variable filter in six modes (LP12, HP12, BP12, NOTCH, LP24, MORPH); EDIT 3: KYBD (key tracking, off by default as on the originals) and PW0 (the pulse's rest width). |
| **ANALOG** | Virtual analog: two band-limited oscillators, PWM, noise, drive, and eight filter models (below). |
| **ACID** | A classic acid bass line: ladder, accent and its sweep, slides. |
| **STRINGS** | A 1970s string machine: 16', 8', 4' registers through a three-line ensemble. |
| **MACRO** | Four macro-oscillators: CHORD, SWARM, ADDITIVE, SPEECH, with a low-pass gate. |
| **PHASE** | Phase distortion: a sine read through a bent phase, resonant and shaped waves. |
| **VPS** | Vector phaseshaping (Kleimola et al., DAFx 2011): phase-distorted waves to formant sweeps, alias-free. |
| **TERRAIN** | Wave terrain synthesis: an orbit over eight surfaces, slowly drifting timbres. |
| **PHYS** | Physical models played as a synth: resonator plucks, keys, bowed pads and strings, basses and leads on plucked, bowed and sympathetic string models and a modal body (after Mutable Instruments Rings and DaisySP). |
| **TRIO** | The 8-bit home computer sound chip: three oscillators, ring, sync, a multimode filter. |
| **WHEEL** | A tonewheel organ with nine drawbars. |
| **VOICE** | Formant synthesis: sung vowels. |
| **NOISE** | Noise played as notes: pitched shift-register tones for basses, keys, brass and leads, and breath and air through a resonant band for pads and soft leads. |
| **GRAIN** / **SAMPLE** | Granular clouds and sample pads, choirs and leads: on four sources of their own, from Clipper (VOX, a small choir opening "oo" to "ah"; GLASS, a pure glassy sustained tone; PAD, seven saws whose filter opens; AIR, breath on the note's harmonics), synthesized, not recorded. |

**Shaping the sound** (every engine): a 24-slot **modulation matrix** (LFO 1, 2 and 3, ENV 1 and 2, velocity, key, mod wheel, aftertouch, random, MPE slide, the **MOD SEQ**'s four lanes, the four macros) onto pitch, cutoff, shape, amp, pan, LFO 2's rate and the engine's own eight values; **LFO 2**, **LFO 3**, **ENV 2** (one-shot or looping); four **MACROS** (BRITE, TIMBR, MOTN, SPACE) on CC 16 to 19; **SPREAD**, each voice its own place in the stereo field; the **MOD SEQ**, a four-lane modulation sequencer; **eight filter models** on ANALOG, WAVE, VECTOR and PONY (SVF, LADDR the ladder, SK a screaming 2-pole, DIODE the diode ladder, WARM a gentle state variable, NOTCH, BP, HP); **VINTAGE** (each voice drifts and differs, like an analog poly); an **INSERT** effect per sound (SOFT, HARD, FOLD, FUZZ, CRUSH, PHASER, FLANGER, CHORUS, the PONY chorus, and since 1.1 PITCH, PIT2, PHAS6, DELAY and LOCMP).

**Effects**: distortion, stereo chorus, a tempo-synced delay (MONO, PING-PONG, TAPE), four reverbs (ROOM, PLATE and HALL after Dattorro, SHIMMER an octave up), a DJ filter, a master three-band EQ, a bus compressor and a look-ahead limiter. Changes glide instead of clicking.

**Tuning**: the reference **A4 from 400 to 480 Hz** (432, 415, 442...), and equal, just, Pythagorean, meantone, Werckmeister III, Kirnberger III, the maqams Rast and Bayati, slendro, pelog, 19 / 24 / 31-EDO, and any **Scala `.scl`** scale from the editor.

**Playing**: 8 voices (12 or 16 as a build option, on both processor cores); POLY, MONO, LEGATO, UNISON and poly unison (2 or 4 voices a note), glide; **LAYER**, **SPLIT** and **MULTI** (two parts on two MIDI channels); an **arpeggiator** with 16 modes, **INCL** and a **Turing machine** (TURNG) among them, **SPICE / DICE** variations, quick controls on ARP + a key and the sustain pedal holding the arpeggio; a step sequencer with chords and recording; scales and chords on the keys, KBD HOLD; **MPE**; MIDI over USB and TRS (pitch bend with its range, mod wheel, expression, sustain, channel and polyphonic aftertouch, macros on CC 16 to 19, program changes and banks, the channel mode messages, local control, CC out: [docs/midi.md](docs/midi.md)); the FM-1 is a USB audio interface too.

**Working**: **UNDO** and **COMPARE** on the FM-1, favourites, **MORPH** between two versions of a sound, a **randomizer** by section, user presets and projects that keep everything above.

**The web editor** (Chrome or Edge, nothing to install): every parameter live; a pro step sequencer (tools, generators, velocity lanes, a pattern library); the whole HYBRID and FMX patches; the MOD SEQ's lanes; the tuning and `.scl` files; `.syx` banks; your own wavetables; a sound library, undo / compare / A-B / randomize tools; backups of the whole FM-1.

## New in Stallion 1.0: matrix patching

- **The MATRIX page, easier to read** (LFO group): the selected cable on a large line, for example `LFO1 > CUT +62`, in its source's colour (pitch blue, gate yellow, envelope green, LFO and modulation orange, performance purple, +V white); the six cables of its half below, the number and source in their colour. SOURCE and DESTINATION step through every code once, grouped by module in the order the web editor's patch bay reads, left to right. On MARE the page says what is normalled: **CUT2 FOLLOWS CUT1** while no cable goes into CUTB, **VCO2 FOLLOWS VCO1** while a cable goes into PIT1 and none into PIT2. **MATRIX 2 > CLEAR**: turned right twice within 1.5 s, it takes out all 24 cables (UNDO brings them back).
- **New sources**: **GATE** (the key held, or the pedal holding it), **E2END** (ENV 2 back at 0 after its release; also high once ENV 2's decay to a SUS of 0 has run out), **CC A** and **CC B** (any MIDI CC, GLO > MIDI 3: see [docs/midi.md](docs/midi.md)), and **S&H**, **NOISE** and **+V** (a constant: an offset).
- **New destinations**: **TRIG1** and **TRIG2** (restart ENV 1 or ENV 2 when the cables into it rise past a third of +V), **SYNC1** and **SYNC2** (restart LFO 1 or LFO 2), **ARP** (the arpeggiator's rate, stepped in speed order: 1/4, 8T, 1/8, 16T, 1/16, 1/32), **DLY**, **REV** and **CHO** (the sound's sends), **LFO1** (LFO 1's rate), **E1AMT** (the ENV DEST depths), and on MARE **CUTB** (its second cutoff), **FLTFM** and **RING**.
- **Triggers**: the key still starts both envelopes; a TRIG cable adds restarts on top and never ends a note. TRIG2: the attack and decay run to the end, then ENV 2 holds while the input is high and releases when it is low. With ENV 2 LOOP on and a TRIG2 cable, an input that is low at the end of the decay releases instead of looping. E2END into TRIG2 loops the whole ADSR.
- **Part-wide destinations**: ARP, DLY, REV and CHO follow the newest voice's cables (as PAN does), and keep their last value while nothing sounds. **ARP moves part 1's arpeggiator only** (there is one): an ARP cable in the layer's sound does nothing, and the MATRIX page says **LAYER ARPEGGIO: UNUSED**.
- **SUST = PATCH** (GLO > MIDI 3): the pedal no longer holds notes, it only feeds CC A or CC B; letting go of a pedal pressed before the change still frees the notes it held.
- **MARE**: EDIT 3 has **FLTFM** (VCO 2 on both filters' cutoff at audio rate) and **RING** (VCO 1 times VCO 2), and MODE has **SPLT** and **SPL+N** (VCO 1 alone into the left filter, VCO 2 alone into the right). User presets keep FLTFM and RING at half resolution (64 steps: a value may come back one step off); projects keep them exactly.
- **The web editor's patch bay** (every engine): drag cables between 64 jacks to patch the matrix, coloured as on the FM-1; the MATRIX tab shows the same cables as a table; up to 24 cables per sound.

## New in Stallion 1.0: more INSERT types

- **Five more INSERT types**, the ones a modern 8-voice poly's 17 multi-effects had and the INSERT lacked: **PITCH** (a pitch shifter, -24 to +24 semitones, FINE in 5-cent steps, FDBK for a rising or falling cascade), **PIT2** (two shifted voices, PTCH1 and PTCH2, and their BAL), **PHAS6** (a six-stage phaser: three notches, swept by a sine from 100 Hz to 5 kHz), **DELAY** (a short delay of the sound's own, 2 to 92 ms with FDBK and TONE: slapback, doubling, combs, beside the long DLY send; the sound runs on while its echoes ring) and **LOCMP** (a lo-fi compressor: COMP, then LOFI's bits and sample rate, then TONE). They come from other open-source FM-1 firmwares, with their notices: the pitch shifter from [TripDeck](https://github.com/anonymousacademe/FM-1-TripDeck) (GPL-3.0), the phaser from [CS-1](https://github.com/twochannels/CS-1) (GPL-3.0), the compressor's law from Airwindows Pop3 as [Lunar Modulator](https://github.com/ip2k/lunar-modulator) ports it (MIT): see [LICENSING.md](LICENSING.md). The INSERT's state moved to the pool (4.6 KB a sound). User presets keep LOCMP (TYPE 16) with the fifth bit in the bank's tail; SLOOP and older versions read it as OFF. The web editor names their values. Tests: `tests/insert_mfx_test.c` (each one measured: the pitch, the notches, the echoes and their tail, the squash), and `fx_sweep_test` sends a sine through them.

## Clipper 1.3.1: the newer flash chip

FM-1 units sold recently (mostly on AliExpress) carry a UC25HQ80 flash chip (JEDEC ID 0xB36014) instead of the Puya P25Q80 (0x856014). Stallion, its USB rescue and its update loader know both, so these units can save and install.

## Clipper 1.3: a bug-fix release

A bug-fix release. Thanks to everyone who reported issues on GitHub: every fix below has its own test, and the whole firmware was checked again (the editor's protocol, MIDI in and a damaged flash fuzzed, every engine, sound and key measured).

**Cleaner sound**
- No more distortion at the end of notes. The mix rounded every part down to 16 steps of the 16-bit output, so a fading note carried a buzz that followed it, on the 3.5 mm output and on USB. The mix now rounds properly: about 10 times less error, the quiet tails are clean.
- UNISON no longer stacks its voices in phase. Most engines started every unison voice at the same phase, up to +18 dB louder than POLY on some sounds, and FMX and MACRO peaked far over full scale. Each voice now starts apart, as a unison should, and the level matches POLY.
- PHYS strings are in tune. They played sharp, by up to a semitone on the low notes of HARPSICH (STR PLUCK, BANJO, PLUCK BASS, the guitars). Now within 2 cents; KOTO, SITAR and TANPURA bend with their curved bridge, as designed.
- The top of the keyboard follows the keys. NOISE's NZ ARCADE, SAMPLE and GRAIN played one pitch over the upper keys (SAMPLE CHOIR from A6, VOX CLOUD from G6); they now go on rising. A GRAIN note played while the one before is fading out is no longer almost silent.
- VECTOR: changing the FILTER MODEL while a note plays no longer gives a loud click.
- FMX: a voice taken back from another note now starts clean (before, it kept the old note's state: a DC offset and a changing timbre).

**Controls**
- A firm press on a layer button (GLO, EDIT, ARP, SEQ, SEL) opens its pages. Before, a press longer than 0.45 s opened nothing; now up to one second counts as a tap.
- On REC READY, a knob changes only its own value: KNOB 3 (START) no longer moves the loop LENGTH too. The same fix covers every page: a knob turned alone changes one value.

**Your data**
- Projects loaded from SLOOP keep Clipper's own values (SPREAD, LFO 3, ENV 2 LOOP, the macros, matrix slots 7 to 12, the delay MODE) across a power cycle.
- Editor backups now include your user wavetables (WAVE USR1..4), which a restore used to erase, and every value of your user presets.
- Safer loading: a user drum kit with an odd root note, or a project file with out-of-range velocities, plays normally.

**Install and go back**
- Return to the official firmware accepts M-VAVE's new V16 (and V15). M-VAVE now offers only V16, which the installer refused.

**Compatibility**
- Projects, presets, samples and settings are kept when you update, and the project format is unchanged.
- A few sounds change a little on purpose: the PHYS strings (now in tune), the unison sounds, NZ ARCADE.

## Clipper 1.2: SLOOP 2.8.3's fixes, a steadier second core, a wind controller as one voice

- **A note held under others keeps sounding** (after SLOOP 2.8.2 and 2.8.3): a key held while you play or record over it is never restarted nor cut by the sequencer, live, in a free take or an overdub, on POLY and on UNI2 / UNI4; a hold overdubbed over a recorded melody goes on through its notes, and the notes the sustain pedal holds are left alone too. Recording a key over a held one on a MONO or LEGATO sound says **MONO: SET VOICE POLY** (once).
- **SEQ > NOTES: LEN under later notes**: a note's length can run on under the notes after it (a long bass under a melody, programmed); a chord's notes and DELETE follow.
- **No stuck notes when MIDI comes in too fast**: when a note message does not fit, the notes MIDI holds are ended once what came is read.
- **SAMPLE and GRAIN: a sample played again starts from silence with its ATK**: no click when a note is played again on MONO, LEGATO or UNISON, or the same note on POLY.
- **No crash on FMX when ALG is turned past 32** (FM KICK and the other FMX sounds: the screen read past the end of a names list and showed FELUCCA CRASH).
- **The second core, hardened**: a backup over USB could crash an FM-1 running on two cores (a red screen until a power cycle). Every job core 1 runs is now checked: a wrong one is refused and the FM-1 goes on with one core until it is switched off, and a fault beside core 1 no longer freezes it. If a unit still does it: HOME (held) → SYS → 2ND CORE OFF.
- **A quick tap no longer flashes a layer**: SEQ, SEL, EDIT, ARP and GLO held show their layer after 0.35 s (it was 0.14 s), so a tap opens the pages cleanly.
- **Room in the flash**: SLOOP 2.8.3's fixes did not fit the FM-1 any more. The screens', the editor's and the projects' code is now compiled for size (as SLOOP does since 2.7), and the drum machine Clipper inherited from SLOOP, which it never played (no pages, silent, hidden in the editor), is left out of the FM-1 image: 16 KB. The metronome stays.
- **The editor without internet**: the installer page downloads the editor as one file (open it in Chrome or Edge, no internet needed), and the site installs as an app that keeps the installer, the editor and the firmware offline (Android too).
- **A wind controller plays one voice**: with GLO > MIDI 2 > BRTH on, the instrument plays one note at a time, legato, whatever its VOICE (a wind controller is a monophonic instrument: a soft note no longer adds a second one at full level); the notes' velocity is fixed and the breath alone shapes the level. (Asked for by a wind-controller player.)

## Clipper 1.1: ultra-compatible MIDI, the SLOOP 2.7.2 sequencer, and a clean reverb

- **Fixed: the crackle on the left channel with the reverb on** (heard by several players, not on the original firmware). **PLATE, HALL and SHIMMER** read one of their left output taps 1417 samples back on a 1334-sample line: out of the line, 83 samples old or in the line before it, a click every 60 ms on the left only (the right's taps were all inside). ROOM did not have it. A second case is fixed too: **the room reverb overflowed** with DAMP turned down (under ~35) and a loud sound (LEVEL and REV up, a chord), its loop's 32-bit products wrapped and the tail turned into a full-scale crackle that went on after the notes. 1.1 also bounds the part and master stages the same way, and moves the room reverb's lines out of the SRAM next to the second core's stacks into the main pool. Every other effect was checked the same way (`tests/fx_sweep_test.c`: a sine through every reverb type, delay MODE and TIME, the chorus, the swept INSERTs, the PONY chorus and the master at their extremes, also under AddressSanitizer): none had the bug; two engines got a guard against reading out of their tables (LOFI's arp, WAVE's frame). If a noise remains, the tests that tell where it comes from are in [QUICKSTART.md](QUICKSTART.md) (A noise on one side?).
- **Made for MIDI controllers and DAWs** (the whole list: [docs/midi.md](docs/midi.md)): **program changes** load the sounds (with **bank select**, CC 0: program *p* of bank *b* is the list's sound *b* × 128 + *p*; GLO > MIDI 2 > PROG turns them off); **CC 11 expression** is the sound's volume; **CC 120 / 123** stop every note (a DAW's panic button), **CC 121** resets the controllers; the **pitch bend range** is GLO > MIDI > **BEND** (AUTO, or 1 to 24 semitones) and **RPN 0** sets it; **LOCAL** OFF (and CC 122) for a DAW that sends the track back; **CC 76 / 77 / 78** the LFO's rate, vibrato and fade; GLO > MIDI 2 > **SEND CC**: the knobs send their CCs, a DAW records and plays back the moves; GLO > MIDI 2 > **BRTH**: a **breath controller** (CC 2) plays the volume, and with V+AT the aftertouch too (the cutoff follows the breath; asked for by a wind-controller player); Clipper answers the **MIDI identity request**. The knobs no longer miss steps when turned fast (SLOOP 2.7.2's encoder).
- **A MIDI channel for the synth** (GLO > **MIDI**, a setting of the FM-1): **IN CH** OMNI as before, or 1 to 16, so a sequencer keyboard's other tracks or a DAW's other channels no longer play Clipper; **OUT** the channel its keys, arp and sequencer send on. (Asked for in a comment.)
- **The sequencer of SLOOP 2.7.2 on the FM-1**: SEQ tapped is the **NOTES** page, a piano roll of the pattern where each note is edited (KNOB 1 the note, a chord's one by one; 2 the pitch; 3 the length; 4 the force, GHOST to HARD and ACC; PRESETS the zoom, 8 to 64 steps) and **OCT+ held** opens its sheet (SLIDE, NUDGE, FILL, MOVE, COPY, DELETE); EDIT tapped deletes the note. The keys and **a MIDI keyboard** write the step under edit (MENU > **SEQ** > **STEP ADVANCE** OFF: the cursor stays, for trying a chord again). **EDIT, ARP and GLO are layers again** when held: EDIT erases a note from the pattern (as it plays, or all of it stopped) and turns SHIFT, LENGTH x2 / half, TRANSPOSE, with undo / redo on OCT- / OCT+; ARP repeats a key on the grid (note repeat, recorded as ratchets); GLO calls a fill (key 9 held, key 10 the next bar), taps the tempo (key 16) and sets the level (KNOB 1). SEQ + a step held + KNOB 2 sets its velocity. Recording: several notes held each go on until their own key is let go, and on a MONO or LEGATO sound a note played over the one before is recorded and played back legato (no new attack). A MIDI clock master's tempo reads steady (140 stays 140). FX and SAVE held still let the keys play.
- **A pro sequencer in the web editor** (Sequencer tab, works with the FM-1's step sequencer as it is): **undo / redo** (Ctrl+Z / Ctrl+Shift+Z, 64 steps, edits by hand too); a **range** (shift-click on the grid) for every tool; **transpose** (±1, ±12), **shift** round, **reverse** (chords keep their length), **double** the length, **copy / paste** (Ctrl+C / Ctrl+V), clear; **chords** from each step's note (maj, min, 7, m7, sus, add9... or the key's own triads and sevenths), accents and slides on a range; **generators**: Euclidean rhythms, random melodies in the key (density, leaps, ties, accents, slides, chords, a seed to get one back), 22 ready-made patterns (acid, octave, offbeat and rolling basses, arpeggios, Berlin school, chord stabs and progressions, gates) and a **pattern text** to write your own (`1 3 5 8'`, `1+3+5>`, `-` tie, `.` rest), **humanize** (velocity and nudge); **lanes** to draw velocity, nudge, ratchet (x1 to x4) and level (GHOST to HARD), and ratchet / level columns in the step list; a **pattern library**: a bank in the browser, `.clipper-pattern.json` files (notes, ties, accents, slides, velocities, ratchets, levels, nudges, fills, locks, length).
- **The PRESETS page filters by engine** (SAVE > PRESETS, KNOB 2: ALL, one engine, or USER): KNOB 1 and the PRESETS knob then go through that engine's sounds only, and the list shows only them, with their count (before, KNOB 2 only jumped to the next engine's first sound). (Asked for in a comment.)
- **The factory sounds load dry**: no delay and no reverb send (users asked for it); FX > DLY / REV, or the SPACE macro, add them, and your user presets keep what you saved.
- **A4 in Hz** (GLO → TUNING → **A4**, 400 to 480 Hz, and in the editor's Tuning card): 432, 415 (baroque), 442 / 443 (orchestras)... under every tuning system, kept with the settings. (Asked for in a comment on the launch video.)

## New in Stallion 1.0: engines and filters

- **MARE** (engine 26, a semi-modular paraphonic voice; Clipper's own code): two VCOs a voice with a continuous **WAVE** (TRI, SAW, SQR, a narrow PLS; SHAPE moves it), **VCO2**, **DTUNE**, **SY/FM** (right of 0 hard sync, ENV DEST SHP sweeping it; left linear FM); two 4-pole **ladders**, the left one left and the right one right: each voice in stereo. **SPACE** works as the instrument's SPACING: it moves VCF 1 alone (the left low-pass, or the high-pass in PARA and SERI), clockwise up, while VCF 2 stays at CUT, and the two sides stay equally loud (balanced stereo). **MODE** STER (two low-passes), PARA (a high-pass left, a low-pass right), SERI (high-pass then low-pass), each with noise (+N). 32 sounds built from the routings and techniques of the instrument's patch books: basses, leads, pads, sequences, drones, keys, strings and brass.
- **FILTER 2** (FX → **FILTER 2**: MODL, CUT, RES, ROUTE; FX → FILTER → **LINK**): a second filter per sound, any of the nine models (OFF by default: the sound as before, bit for bit). **ROUTE**: **SER** (after the first filter: an LP and an HP make a band-pass), **PAR** (beside it, on the same oscillators: two peaks), **SPLIT** (the first filter left, FILTER 2 right: each voice in stereo). With **LINK** on (the default) its CUT is an offset from the first filter's cutoff (shown as **OFS** in semitones: one knob, FILTER's CUT, sweeps both); off, CUT is its own. It follows ENV → FLT, LFO → FLT, velocity, the mod wheel and the matrix's CUT as the first filter does, and the matrix has two new destinations, **CUT2** and **RES2**, for it alone. On ANALOG, WAVE, VECTOR and PONY it sits beside their filter; on the other engines it filters the voice after the engine (SER; PAR mixes the engine's own sound with it, SPLIT puts the engine left and FILTER 2 right). A change of MODL or ROUTE under a held note dips and comes back. Not in the web editor yet (FILTER 2's values are the FM-1's own, as SLOPE's; user presets and projects keep them).
- **SLOPE** (FX → FILTER → **SLOPE**: 12, 24, 48 dB): the 4-pole filter models (LADDR, OTA, DIODE, and PONY's OTA) at 12 dB (the second stage's output with the resonance loop still closed at the fourth, as the classic OTA polys' 12 dB), 24 dB (as before, bit for bit) or 48 dB (four more poles after them at the same cutoff, the resonance's peak and the bass kept within a few dB). The 2-pole models (SVF, SK, WARM, NOTCH, BP, HP) keep their own slope. A change under a held note dips and comes back (no click). Set on the FM-1 (the web editor's protocol has no parameter number left: SLOPE is the FM-1's own; user presets and projects keep it).

## Clipper 1.0: a stereo voice, a deeper modulation, four macros, a hall and a shimmer

- **SPREAD** (VOICE 3 → **SPRED**, every engine): each voice gets its own place between left and right (the outer voices first, the next ones between), so a chord opens across the stereo field and the voices of UNI2 / UNI4 fan out. The middle stays what it was (left + right is the sound without SPREAD, bit for bit at 0), and the side goes through the same DIST / INSERT level and track FILTER. The pads and the unison sounds come with a little of it (48).
- **The matrix has 12 slots** (MATRIX 1 .. 12, on the FM-1 and in the editor), and new sources: **LFO 3** (LFO 3 page: RATE, WAVE, MODE FREE / KEY) and the four macros **MAC1 .. MAC4**.
- **ENV 2 LOOP** (ENV 2 LOOP page): ONE as before, or **LOOP**: past its decay it starts its attack again, as long as the note is held: an envelope that cycles, a rhythmic modulator in the matrix.
- **Four MACROS on every engine** (EDIT → **MACROS**; MIDI CC 16 .. 19; HYBRID keeps its own M1..M4): **BRITE** (the cutoff, ±2.5 octaves), **TIMBR** (the engine's shape), **MOTN** (a vibrato and LFO 3 on the cutoff: motion), **SPACE** (more reverb, delay and SPREAD). They are matrix sources too.
- **HALL and SHIMMER** (REVERB → TYPE): HALL is the plate's tank made a room of stone (a longer decay, a pre-delay, a darker top); SHIMMER feeds it an octave up, the pitched tail of the ambient records. A change of TYPE fades the old tail out (no click). **SHIM** (REVERB 2) adds that octave to any TYPE, as much as you like: ROOM, PLATE and HALL get a shimmer of their own (0, the default, leaves them as they were; SHIMMER is HALL with SHIM all the way up).
- **The delay has a MODE** (DLY 2 → **MODE**): **MONO** as before, **PING** (the first echo left at TIME, the next right at twice TIME, back and forth), **TAPE** (wow and flutter swing the echoes; the repeats saturate as they pile up). A change of MODE cross-fades.
- **Kept with the sound**: user presets keep every 1.0 value in a tail after the bank (SLOOP and older Clipper versions read the bank as before and never see it); projects keep them in track 3's nudges while track 3 has no steps (a project whose track 3 has steps loads them at their defaults).
- **Fixed**: the arp turning itself on while browsing. Four sounds bring an arp with them (8BIT RUN, ACID RUN, STEP PLUCK, TURING PONY), and since 0.7 a running arp went on through sound changes, so passing one of them left the arp on for every sound after it. Now an arp a sound brings goes off with the next sound; only an arp you turn on (or change) yourself runs on.
- **Fixed**: the user presets' 1.0 values (SPREAD, LFO 3, the macros, slots 7 to 12...) were not read back after the FM-1 was switched off, and the boot wrote past the end of their table (a slip in 1.0's first build).
- **Fixed**: the PONY chorus (CHOR I / II / I+II, and the INSERT's PONY types) cracked on a loud chord of many voices (its arithmetic wrapped round past 16 bits; now 64-bit products, and the line keeps an eighth of the sound instead of a quarter: no clipping either).
- **Simpler**: no PROJECT page on the FM-1 any more (Clipper is one instrument: the autosave keeps what you are playing, the user presets keep your sounds); SAMPLE and GRAIN play Clipper's four built-in sources only (USER SMP and USER GRAIN are gone: 407 sounds); in the editor the Samples tab became **Wavetables** (WAVE's USR1..USR4) and the Projects tab **Backup**.
- **Changed**: the undo history moved out of the 96 KB RAM region (the new buffers needed the room).
- **The web editor**: the new pages (VOICE 3, LFO 3, ENV 2 LOOP, MACROS, DLY 2) and the matrix's 12 slots, with Clipper 1.0's layout (126 values, P_E0 118; still protocol 14). A 0.9 sound file loads into 1.0 with the new values left as they are.

## Clipper 0.9: filter models, VINTAGE, microtuning, a Turing machine, and a clean sound

- **Filter models** (FILTER → **MODL**, on ANALOG, WAVE and VECTOR; HYBRID keeps its own two filters): **SVF** (12 dB, as before), **LADDR** (the 24 dB ladder), **SK** (2-pole Sallen-Key style with the diode clipping in the feedback: it screams at high RES and stays stable), **DIODE** (the 4-pole diode ladder: rubbery, less bass loss), **WARM** (a 12 dB state variable, gentle resonance), **NOTCH**, **BP** and **HP**. Zero-delay-feedback models, integer DSP; each costs between 0.6 and 1.4 times the old filter. A model changed under a held note dips and comes back (no click).
- **VINTAGE** (VOICE 2 → **VINTG**, every engine): the voices of an analog poly are never quite alike. Each voice gets its own small tuning error and filter offset (its "voice card", fixed), a slow random drift of its pitch, and each note a little of its own. At 127: the card ±6 cents, the drift ±5, the note ±2, the cutoff ±1.5 steps. 0: bit for bit as before.
- **TUNING** (GLO → **TUNING**, for every sound, kept with the settings): **EQUAL**, **JUST** (5-limit), **PYTHA**gorean, **MEANT**one (1/4 comma), **WERCK**meister III, **KIRNB**erger III, the maqams **RAST** and **BAYAT**i (neutral thirds), the gamelan's **SLEND**ro and **PELOG**, **19**, **24** and **31-EDO**, and **USER**: any **Scala .scl** scale (up to 48 notes per period, any period: Bohlen-Pierce's tritave too) sent from the web editor. ROOT sets the tonic. A glide moves through the scale.
- **TURNG**, a Turing machine in the arpeggiator (ARP MODE → TURNG): a looping shift register picks among the held notes. On the SPICE page, **MUTAT** is how often a bit flips (0 locked, 127 random), **SEED** another pattern, **LOOP** its length (1..16). **LOOP** also sets how many notes SPICE / DICE vary before repeating.
- **A clean sound, no clicks** (a new test, 46 cases, renders every change and checks that no sample jumps): the master limiter's gain now moves on an S-curve; the sends (CHOR / DLY / REV) and the INSERT's DIST glide instead of jumping; the delay's TIME cross-fades the old and new tap; the chorus DEPTH glides; the master FILT goes through the middle when it crosses from LP to HP; FLANG / CHOR in the INSERT wait for their line to fill; a WAVE table, an FMX patch or a filter model changed under a held note dips and comes back.
- **GRAIN and SAMPLE play out of the box**: four sources of Clipper's own, synthesized (no recording, 25 KB of flash): **VOX** (a small choir, "oo" opening to "ah": GRAIN's POS scans the vowel), **GLASS** (a glass harmonica), **PAD** (seven saws, the filter opening), **AIR** (breath on the note's harmonics). Ten GRAIN sounds (VOX CLOUD, VOWEL DRIFT, ANGEL HALO, GLASS RAIN, CRYSTAL SWELL, FROZEN PAD, DARK DRONE, SCAN PAD, WIND CHOIR, BREATH GRAIN) and four SAMPLE ones (SAMPLE CHOIR, GLASS HARP, SAW ENSEMBLE, AIR FLUTE). SET / SRC 0..3 are still your slots USR1..USR4; the sources come after.
- **17 new sounds** (those, TURING PONY, HOWL VECTOR, DIODE VS BASS), and VINTAGE on a few PONY pads. **409 sounds, 23 engines.**
- **A sound pass over every preset** (each one rendered, measured and its spectrum looked at): DIGITAL's FM no longer folds back as noise on high notes (its index held under the Nyquist limit, and gliding through each block: no step when a mono bass retriggers); CLAV less harsh; the master EQ's shelves glide their corners too.
- **A new logo**: a clipper under sail, in the line of SLOOP's (its one sail becomes four, one per track colour), on the boot screen, the installer, the editor and here.
- **Fixed**: a SLOOP 2.6 project loaded on Clipper read its MOD SEQ / SPICE values from past the end of its INSERT's (now their defaults).
- **The web editor** (protocol 14): a **Tuning** card (system, root, load a .scl), and the new knobs (VINTG, LOOP, MODL's models, TURNG).

## Clipper 0.8: PONY and its chorus, VECTOR, a modulation sequencer, SPICE / DICE

- **PONY**, a classic 1980s DCO poly synth (the idea, nobody else's code): one DCO per voice with a **saw**, a **pulse** whose width the LFO sweeps (PWM: LFO DEST SHP, the wheel, the matrix's SHAPE) or both together (**S+P**), a **sub** square an octave down and **noise**; the **HPF** in four positions (0 a little bass boost, 1 flat, 2 and 3 thinner), the **24 dB resonant ladder** (keyboard tracking, its envelope), and the **chorus**: **I** (0.5 Hz, wide), **II** (0.8 Hz, deeper), **I+II** (8 Hz, shallow, both buttons together): a stereo BBD chorus, two taps of one line swept in opposite directions, darkened like the bucket brigade, left = dry + one, right = dry + the other. A mode changed under a held chord fades (no click). EDIT: WAVE PW SUB NOIS / HPF CUT RES CHOR. 16 sounds (pads, brass, strings, bass, keys, synthwave lead, a siren).
- **The PONY chorus on any sound**: the INSERT has three more types, **BBD1**, **BBD2**, **BBD12** (A WIDTH, B TONE, C NOISE: the BBD's hiss, MIX the wet).
- **VECTOR**, vector synthesis with a joystick-style mix: four waves at the corners of a square (**A B C D**, 16 of Clipper's own: SINE TRI SAW SQR PLS25 PLS10 ORGAN VOX A VOX O BELL HOLLW BRASS RESO DIGI STRNG METAL, each in three band-limited copies), a point in it (**X**, **Y**) mixes them, and **MOVE** sends the point round a path by itself from each note's start: CIRC, 8, SWEEP (from A to the point: a mix envelope), LOOP (A B D C), RND, each slow or fast (+). SHAPE moves X; the matrix moves X and Y by name. Then a 12 dB low-pass. EDIT: A B C D / X Y MOVE CUT. 16 sounds.
- **MOD SEQ**, a modulation sequencer in every sound (LFO → **MOD SEQ**, **MOD SEQ 2**): four lanes of 16 steps (-8..7) that step at **RATE** (1/32 .. 2 bars, the song's tempo), over **LEN** steps, each lane gliding (**SMTH**), from each first key (**START KEY**) or on the song's grid (**SYNC**). The matrix reads them as four more sources, **SEQ1..SEQ4**: a lane to the cutoff, another to the pitch, a rhythm to the level, any EDIT value by name. On the FM-1: LANE STEP VALUE and **SHAPE** (RAMP+ RAMP- TRI SINE SQR RND FLAT INVRT SHIFT: scroll it to audition), the lane drawn big, the step playing lit, the four lanes small below. A sound loaded brings four shapes to start from (a ramp, a triangle, a rhythm, a random line). Kept with the sound: UNDO, COMPARE, the layer's SWAP, user presets, projects.
- **SPICE / DICE** (ARP → **SPICE**): the arpeggiator's notes and the sequencer's steps vary as much as SPICE says: some rest, some jump an octave, the velocities and the gates move; the same way each time round (16 notes, or the pattern's steps), until **DICE** is turned (another variation). What is recorded is never changed. SPICE 0: as before, note for note.
- **The web editor** (protocol 13): a **MOD SEQ** card (the four lanes as bars to drag, the shapes, LIVE), the new pages (MOD SEQ 2, SPICE, the INSERT's PONY types), PONY and VECTOR.
- **Both new engines run on both cores** (bit for bit, as the others). **392 sounds, 23 engines.**
- **Fixed**: a user preset sent from the web editor was written in SLOOP's layout over Clipper's record (its LFO 2, ENV 2, matrix and INSERT lost, and a few bytes past the record).

## Clipper 0.7: both cores, a history of the sound, and SLOOP 2.7's best

- **The FM-1's second processor core renders half the voices.** After SLOOP 2.7 (the second core's start-up is [X0X](https://github.com/charlesvestal/fm1-x0x)'s, Charles Vestal, after [Melodee](https://github.com/keremimo/melodee); the HAL came to SLOOP through Andre Rezemini's dualcore branch, 2.7.1's safe hold included). SLOOP splits by track; Clipper plays one sound, so it splits **a sound's voices**: this core works out every voice's modulation, hands the second half of the voices to core 1, renders the first half, waits, adds. Bit for bit the same sound whoever renders what (the host tests prove it for every preset, one core or two, a slow second core, a lost job, under ThreadSanitizer). Engines that may split: HYBRID, FMX, WAVE, ANALOG, FM6, VPS, TERRAIN, WSEQ, MACRO, STRINGS, TRIO, PHASE, DIGITAL, LOFI (0.8: and ACID, PONY, VECTOR; the others stay on one core). MENU → SYS → **2ND CORE** ON / OFF; ABOUT says **2 CORES** or **1 CORE**. This is what should make 12 or 16 voices possible: measure with GLO → SYSTEM (CPU, LATE, PEAK).
- **UNDO and COMPARE on the FM-1** (EDIT → **UNDO**): every change of the sound becomes a step once it rests (a knob sweep is one step), a sound loaded too; KNOB 1 goes back (left) and forward (right) through up to 16 steps, the page lists them ("-1 CUT RES", "-2 THICK BASS"); FMX's patch goes with them. KNOB 2 **CMP**: ORIG plays the sound as it was loaded, EDIT your edits again. HOME says **EDITED** when the sound differs from what was loaded, and loading another sound never loses your edits: UNDO brings them back.
- **RANDOM by section**: WHAT now also offers **ENV**, **MOD** (LFOs, matrix amounts), **FX** (and the INSERT's values) and **OPS**: on FMX, the operators themselves (musical ratios, the modulators' levels, envelopes, now and then a wave, the routing's amounts).
- **Favourites**: PRESETS page, KNOB 4 right puts the sound at the top of the list (up to 16), left takes it out; kept with the FM-1's settings.
- **SLOOP 2.7's gifts**: **KBD HOLD** (ARP 2 → HOLD ON with the arp OFF: the notes go on sounding, the next chord replaces them; HOME shows HOLD), **15 arp modes** (REPEAT, DNUP, UP+8, CONV, DIVG, PINKY, THUMB, WALK, CHORD after UP DN UPDN RND ORD), **ARP SWG** works stopped too (#52), a sound loaded while the arp runs keeps the arp and its notes, **CC 74** sweeps the engine's own cutoff or brightness, **REC QUANT OFF** (KNOB 4 of the REC screen: your timing kept as nudges), overdub over held notes, **knob acceleration** that grows with the speed of the turn (MENU → SCREEN → KNOB ACCEL), **LEVEL and PAN glide** (no zipper), user samples that **loop while held** (LOOP, LOOP+TAIL), **USB RATE** (AUTO, 44.1, 48 kHz: MENU → AUDIO), **MIDI CLOCK OUT** on USB (MENU → MIDI), the crash screen with the stack, 10 s.
- **The web editor**: a **Sound tools** card: **Undo / Redo** (Ctrl+Z, Ctrl+Shift+Z), **Compare with loaded**, **Store A / Store B / Toggle A / B**, a **randomizer by section** with an amount, **what changed since loaded** (a table), and the **envelope drawn** (ENV, and ENV 2 dashed). The Samples page sets each file's **loop** (ONE SHOT, LOOP, LOOP+TAIL with its points on zero crossings, a WAV's own loop read from its smpl chunk). The editor finds the FM-1 under any of its port names (a port kept from another firmware, e.g. "X0X FM-1").

## Clipper 0.6: two parts, an insert per sound, FMX and wave sequencing

- **MULTI (bi-timbral)**: EDIT → LAYER → MODE has a fourth choice, **MULTI**: the layer's sound becomes a part of its own, on its own MIDI channel (**CH**, on the knob SPLIT has in the other modes; 2 by default), with its own pitch bend, mod wheel, pressure and sustain pedal; every other channel, the keys, the sequencer and the arpeggiator play the main sound. The voices are shared (8, or 12 / 16 with `INSTALL-CLIPPER.bat 12` / `16`). LAYER and SPLIT as before.
- **An INSERT effect per sound** (FX → INSERT, INSERT 2), SLOOP 2.6's: TYPE OFF, SOFT, HARD, FOLD, FUZZ (DRIVE TONE LEVEL), CRUSH (BITS RATE LPF), PHASR, FLANG, CHOR (RATE DEPTH FDBK), and MIX. After the sound's drive, before its filter and the sends; the main sound and the layer each their own; kept with the sound (presets, user presets, projects). OFF or MIX 0: the sound bit for bit as before. A SLOOP 2.6 sound or project brings its INSERT along.
- **FMX**, a six-operator engine: each operator its own **wave** (SINE TRI SAW SQR HALF ABS QUART PULSE SIN8 NOISE, the bright ones band-limited) and its own **mode**: **PM** (the classic six-operator synths' "FM"), **FM** (true, linear, through-zero frequency modulation) or **RING**; a ratio (x0.5 .. x31, FINE) or a fixed frequency; level, ADSR, velocity, key scaling, detune. **Any routing**: any operator into any other, each connection its own amount, an operator into itself its feedback, any operator to the output. 40 templates (ALG: the 32 classic algorithms, then CASCADE, 2 STACKS, STAR, 2 x 2>1, RING 3, LOOP, Y, 1 > 5) to start from; change any connection after. A pitch envelope (PEG). EDIT 1 / 2: PTCH ALG MOD FB / TIME DTN VEL KTRK (macros on the patch); on FMX six more EDIT pages edit the patch itself: **OP** (OP WAVE MODE RATIO), **OP 2** (FINE LEVEL VEL KSC), **OP ENV**, **ROUTE** (TO FROM AMT OUT, with the routing drawn as a grid), **FMX** (DTN FIX PEG PTIM) and **STORE** (into the bank, B1..B27, the song stopped). The bank is FM6's: a slot holds an FMX patch or a six-operator `.syx` voice, and FMX plays such a voice converted. In the editor: the whole patch, the routing drawn, templates, LIVE, STORE, files (`.clipper-fmx.json`). 16 sounds. A voice costs about what a HYBRID voice does.
- **WSEQ**, wave sequencing: each note plays a sequence of steps, each its own wave, pitch and level, at the song's tempo, the next one fading in over the end of the one before. SEQ (16 sequences: STEPS RHYTHM SWEEP FIFTHS MINOR MAJOR OCTAVE GATE BELLS 8BIT MORPH VOX NOISY DUB DORIAN PING), RATE (1/32 .. a bar), XFADE, LOOP (FWD BACK PING ONCE), START, PITCH (how wide the steps' intervals: 0 none, 127 twice), CUT, RES. 16 sounds; light (a third of HYBRID).
- **360 sounds, 21 engines.** The web editor knows the new pages (INSERT, and FMX's patch).

## Clipper 0.5.1: an audio timing readout (the crackle hunt)

GLO > SYSTEM, the CPU column now turns every 1.5 s: **CPU** (or the USB status), **LATE** (audio buffers rendered too late since power-on: each one a click) and **PEAK** (the slowest buffer of the last few seconds, % of the time it has; over 100 means late). If you hear crackles, note whether LATE climbs while they happen.

## Clipper 0.5: two more engines, VPS and TERRAIN

Both suggested in the Felucca discussions ([#145](https://github.com/hugelton/Felucca/discussions/145), [#146](https://github.com/hugelton/Felucca/discussions/146)) as light ways to sounds no oscillator makes; both cost about what ANALOG costs (a third of HYBRID).

- **VPS**, vector phaseshaping (after Kleimola, Lazzarini, Timoney and Välimäki, DAFx 2011): a sine read through a bent phase. The bend is one point, **D** (where in the period) and **V** (how many cycles by then). V under 1: phase-distorted waves; V above 1: V cycles squeezed into the first part of the period, a formant at V × the note, as a hard sync or a vowel sweep. Whole cycles only (V between two integers crossfades them): no spike, no aliasing. **VENV** sweeps V with the envelope at each note, **OSC2** adds a second one with D mirrored and detuned (**DTN**), **SUB** a sine an octave down, then CUT and RES. SHAPE modulation (LFO, envelope, wheel, matrix) moves D. EDIT: D V VENV OSC2 / CUT RES DTN SUB. 10 sounds.
- **TERRAIN**, wave terrain synthesis, lite: an orbit circles over a surface and the surface's height under it is the sound. One turn is one period, so it is pitched; where the orbit lies (**X**, **Y**) and how wide it is (**RAD**) choose the harmonics; **ORB** squeezes it flat or turns it into a figure eight; **DRIFT** moves it slowly, each note its own way, for timbres that keep evolving. Eight surfaces of 32 × 32 (**TERR**: WAVES, RIPPLE, CRYSTAL, BUMPS, SADDLE, FOLDS, TWIST, CHAOS), periodic in both directions and band-limited; the orbit narrows on high notes. EDIT: TERR RAD X Y / ORB DRIFT CUT RES. 10 sounds.
- **328 sounds, 19 engines.** Every EDIT value of both is a matrix destination (X and Y moved by LFO 2 or the keys, say).

## Clipper 0.4.1: SLOOP 2.6's fixes, and living next to SLOOP 2.6

- **A cleaner output**: the master limiter looks one block (0.7 ms) ahead, so a loud attack is already turned down when it comes instead of its first samples going through the output's soft clipper (a crackle with MASTER up, in headphones most); a held loud bass keeps one gain (no whistle). After SLOOP 2.6.
- **No click when a filter starts to close**: the sound's FILTER and the master FILT start their low-pass where the signal is (it started from silence under the sound: a crack).
- **FM6**: a sound changed under held notes fades them out with their own patch; the new patch comes after (they took it at once: a click).
- **Arpeggiator** (#191, after Felucca 1.5): a latched arp follows TRN, ROOT, SCALE and QNT changed after its keys were pressed.
- **A project load or NEW** puts the pitch wheel back to the middle, the pressure to 0 and the sustain pedal up (its notes let go); MPE's per-note bends too.
- **Web editor**: a value set again (the same value on another sound, or after a knob moved it on the FM-1) is sent again (it was skipped for good); a sound file from another version loads by layout; a backup with projects newer than the firmware reads stops before writing anything.
- **Installer**: an FM-1 running SLOOP is found (going from SLOOP to CLIPPER no longer stops at *FM-1 not found*); when none is, the message lists the MIDI ports it saw.
- **Going between SLOOP 2.6 and CLIPPER on the same FM-1**: CLIPPER reads SLOOP 2.6's projects (they were shown as empty slots, and saving there wrote over them); it keeps SLOOP 2.6's MIDI settings and MIDI LEARN in the settings, and SLOOP's SYN kits; its MPE setting no longer sits where SLOOP 2.6 keeps GLO > SYSTEM > IN. User presets are one bank again for both (192-byte records: 0.4's first build made them 232, which SLOOP read as empty; they are converted at power-on): the matrix, LFO 2 and ENV 2 ride in the record's spare values, SLOOP sees the sound as it is. A project keeps LFO 2, ENV 2 and the matrix in track 3's free lock slots, which SLOOP passes over (0.4's first build put them in tracks 3 and 4's own values; still read).

## Clipper 0.4: a modulation matrix on every engine, and MPE

- **MATRIX** (LFO → MATRIX), on every engine: six slots, each a **SOURCE** → **DESTINATION** × **AMOUNT** (±). SLOT chooses the slot the other three knobs edit; the screen lists all six. Sources: **LFO1**, **LFO2**, **ENV1** (the amplitude envelope), **ENV2**, **VEL**, **KEY** (the note, around C4), **MW**, **AT** (channel or polyphonic), **RAND** (a new random value at each note), **SLIDE** (MPE). Destinations: **PITCH** (up to ±24 semitones, finer near 0), **CUT**, **SHAPE**, **AMP**, **PAN**, **LFO2** (its rate), and any of the engine's eight EDIT values by name (ANALOG's CUT, WAVE's POS, FM6's ratios...), per note. A slot at 0 costs nothing and changes nothing, bit for bit.
- **LFO 2** (LFO → LFO 2): RATE, WAVE (SIN TRI SAW SQR S&H), MODE: **FREE** (one for the sound) or **KEY** (each note its own, restarted at the note).
- **ENV 2** (ENV → ENV 2): a second ADSR, for the matrix.
- **MPE** (HOME held → SYSTEM → MPE): OFF, or the member channels' bend range (48 ST, the MPE standard, or 2, 12, 24, 96). For an MPE controller: channel 1 is the master channel (the bend, wheel and pedal of every note), channels 2 to 16 each carry one note with **its own pitch bend**, **its own pressure** (the AT source and MOD AT>) and **its own slide** (CC 74: the SLIDE source; with no slot using it, it opens the note's filter).
- **Kept with the sound**: projects keep LFO 2, ENV 2 and the matrix of both sounds (the layer too) in room the synth never used (0.4.1: track 3's free lock slots); a project keeps its size, older ones load with the matrix empty. User presets keep them too (0.4.1: in the record SLOOP reads). RANDOM ALL also moves LFO 2, ENV 2 and the amounts of the slots in use; MORPH moves every one of them.

## Clipper 0.3: playing it like a big synth

- **Master EQ and compressor**: FX → **EQ+COMP**: LOW (a shelf at 150 Hz), MID (a peak at 1 kHz), HIGH (a shelf at 5 kHz), each ±12 dB (MID ±10) in 1 dB steps, and **COMP**, a stereo bus compressor (the "glue": one knob lowers the threshold from -6 to -30 dB and raises the ratio from 2:1 to 4:1, with automatic make-up gain). Flat and at 0 they are bypassed, bit for bit; every change glides, no click.
- **Layer and split**: EDIT → **LAYER**: MODE (OFF / LAYER / SPLIT), SOUND (any of the 308 sounds or your presets as the second sound), SPLIT (the split note: the layer below, the main sound from it up), LEVEL; **LAYER 2**: OCT (the layer -2..+2 octaves), PAN, FILT and **SWAP** (the two sounds trade places: every page then edits the other one; SWAP again to go back). Keys, MIDI, the sequencer and the arpeggiator all play both; a note always ends where it started.
- **Poly unison on every engine**: EDIT → VOICE: **UNI2** and **UNI4** stack 2 or 4 detuned voices on each note (DTUNE on VOICE 2), in POLY. (UNI is still the 8-voice mono stack.)
- **Morph and randomizer**: EDIT → **MORPH**: SET A, SET B, then MORPH moves the whole sound between them (every continuous value; the switches change half way; MIDI CC 20). EDIT → **RANDOM**: AMT, WHAT (EDIT: the engine's 8 values, +ENV: and the envelopes, ALL: and the LFO and the sends), RAND, and BACK to undo.
- **Modulation**: LFO → **MOD**: VEL>F (velocity to the cutoff, ±), MW> and AT> (the mod wheel and aftertouch, channel or polyphonic) to VIB (a 5.5 Hz vibrato), CUT, SHP (the engine's shape), WAH (the sound's LFO on the cutoff) or TREM. Every engine (HYBRID keeps its own matrix).
- **Smooth sound changes**: a preset, an engine, a user preset, SWAP, RAND or BACK changed under ringing notes fades them out over 6 ms with their own values (no click); notes played meanwhile wait for the new sound.
- **WAVE loads 6x faster**: a table's band-limited copies come from an inverse FFT instead of additive sums (the same samples within 2 steps of 32767), four frames per pass of the main loop: a new table is ready in four passes.
- **No full-screen visualiser** (its code and 5.5 KB of RAM go to the synth); HOME keeps its scope.
- **308 sounds**: 99 new ones across every engine (DCO and classic polys, bass pedals, FM, phase distortion, 8-bit chips, tonewheel organs, bowed and plucked models, acid basses, string machines, chords, swarms, speech...), each level-matched through the synth's own output chain.

## Clipper 0.2: the sound

- **The ladder (LADDR)**: a 24 dB four-pole ladder with zero-delay feedback (after Vadim Zavalishin) and a tanh at its input: it drives warm, its resonance saturates softly, and it self-oscillates on pitch. A filter type in HYBRID; on ANALOG, FX → FILTER → **MODL** chooses SVF (12 dB) or LADDR (24 dB).
- **Soft resonance**: HYBRID's state-variable filters saturate softly instead of clipping.
- **Anti-aliasing**: triangles are band-limited (polyBLAMP), the hard sync gets a BLEP at each reset, and FM, sine feedback and the wavefolder are held below 20 kHz on high notes. Measured by `tests/alias_test.c`: folded partials stay under -40 dB (triangles -62 dB, the folder -66 dB), against -7 dB before for a full fold.
- **Plate reverb** (after Jon Dattorro): FX → REVERB has TYPE (ROOM / PLATE), SIZE, DAMP and PRE (pre-delay, ms). The chorus moved to its own CHORUS page.
- **WAVE, wavetables**: 16 tables of 16 frames (classic shapes, PWM, sync, FM, fold, formants, classic digital wavetable sweeps, organ, chip, comb...), POS morphs through them (the envelope and the LFO move it through ENV DEST / LFO DEST SHP), DTN adds a second oscillator, SUB a sine an octave down, then ANALOG's filter (SVF or LADDR) and drive. The tables are stored as spectra and band-limited per octave in RAM, read with a cubic: aliasing stays below -60 dBFS at 2.4 kHz (`tests/wave_test.c`). Your own tables (a WAV of single cycles, 2048 points a frame) go to a user slot from the editor's Wavetables tab; TABL = USR1..USR4 plays them.
- **ACID**: a classic acid bass line engine, one voice. Saw or square (45 %) into the ladder, a one-pole high-pass and DIST. The filter envelope (ENV, DEC), the **accent** (a note at velocity 120 and up, the sequencer's ACCENT steps: louder, the envelope shorter and deeper) and its **sweep** (SWP: a run of accents charges a capacitor that pushes the cutoff further up, the squelch), and **slides**: a SLIDE step, or legato with GLIDE in TIME mode, glides without restarting the envelopes. EDIT: WAVE CUT RES ENV / DEC ACC DIST SWP.
- **STRINGS**: a 1970s string machine. Divide-down registers 16', 8' and 4' locked on one phase, saw towards square (SHP), three ensemble lines moved by a slow sweep (~0.6 Hz) and a fast shimmer (~6 Hz) 120 degrees apart (ENS, RATE), a key-tracked 12 dB low-pass. EDIT: 16' 8' 4' ENS / CUT RATE SHP RES.
- **MACRO**: four macro-oscillator models on three knobs HARM, TIMB, MORPH: **CHORD** (OCT 5TH SUS4 m m7 m9 m11 6/9 M9 M7 M, inversions, saw to square), **SWARM** (eight oscillators spread up to a semitone, each drifting), **ADDTV** (harmonics 1..16 under a moving bump, odd to even, nothing above 0.45 FS), **SPEECH** (a glottal source through three formants gliding along a word: AI AU OI UA WOW YEAH AEIOU MAMA, held or spoken). Then a low-pass and a **low-pass gate** (DEC, LPG). EDIT: MODL HARM TIMB MRPH / CUT RES DEC LPG.
- **No built-in samples**: 300 KB of flash go to synthesis. SAMPLE and GRAIN play your own samples (the editor's Samples tab, four slots). 209 factory sounds in 0.2 (308 in 0.3): five new ones on the ladder (THICK BASS, THICK LEAD, LADDER PAD, MINI BASS, MINI LEAD), sixteen on WAVE, eight on ACID, eight on STRINGS, twelve on MACRO. 17 engines.

## What is left to do

What Stallion needs most, roughly in order:

- **More testing on the device.** The CPU load on the FM-1 (SPREAD, SHIMMER, 8 voices on both cores; 12 and 16 voices), the flash and RAM of the real build, clicks or crackles that only the hardware shows, the storage of user presets after a power cut: all of it keeps being checked on real units: please report what you find.
- **Room on the FM-1.** Its flash is full: the multi sounds (M01..M16) are on the SMK-37 Pro only for now.
- **More room for your own sounds.** The FM6 / FMX patch bank holds 27 patches, not even one whole 32-voice `.syx` bank. The flash freed by the samples and the projects (about 300 KB) could hold 32 banks of 32 voices; the format, the editor page and the way to pick them on the FM-1 are still to be built.
- **The sounds.** Every engine now has a full bank (819 sounds), and the PONY, PALOMINO, STAMPEDE, RODEO and MARE sounds are patched in the matrix. The other engines' banks are still to get the same patching, and the macros should be tuned per engine.
- **The editor.** It shows every parameter, but some things deserve a page of their own (a bank manager for SysEx). It has been used much less with 1.0's layout than with 0.9's.
- **Documentation.** A real manual (each page of the FM-1, each engine), screenshots, short videos; the French and Japanese texts are incomplete.
- **Storage edges.** A project's 1.0 values are kept only while its track 3 has no steps (the format is full); older sounds load with the 1.0 values at their defaults.
- **Feedback.** Bugs, crashes (photograph the **FELUCCA CRASH** screen: it stays 10 s), stuck notes, sounds that are too loud or too quiet, controllers that behave oddly (MPE, CC 16 to 19): please report them in the [Issues](https://github.com/EberonLtd/stallion-fm1/issues).

## On the FM-1

| | |
| --- | --- |
| **HOME** | The synth screen: the sound's name, its family, its number in the list, the engine and a live scope. The four knobs play the sound's main controls (for HYBRID: CUT, RES, M1, M2). |
| **PRESETS** | Every sound, one by one, factory and user presets. On the SAVE > PRESETS page, KNOB 2 filters the list by engine (ALL, an engine, USER). |
| **FX pages** | FX (sends), FILTER (the sound's DJ filter and MODL, the filter model), INSERT, DLY, REVERB, CHORUS, EQ+COMP. |
| **GLO pages** | GLOBAL (tempo, swing, CLICK the metronome, fine tune), MASTER (DUST, the master FILT), **TUNING** (the system and its root, for every sound), SYSTEM (MIDI, sync, and the CPU / LATE / PEAK readout). |
| **ALGORITHM** | Jumps to the next family: PAD, BASS, LEAD, PLCK, KEYS, STAB, FX and the rest. |
| **FX, SAVE** | A tap opens the pages. While one of them is held, the keys still play. |
| **SEQ, SEL, EDIT, ARP, GLO** | Held, they are the sequencer's layers, as on SLOOP 2.7.2: SEQ the 16 steps on the keys (a step held: KNOB 1 its note, 2 its velocity, 3 ratchet, 4 nudge, PRESETS a lock), SEL the key and the chords, EDIT erase (a key: that note out of the pattern) with SHIFT, LENGTH x2 / half and TRANSPOSE on the knobs and OCT- / OCT+ undo / redo, ARP note repeat on the grid, GLO a fill (key 9 held), a fill bar (key 10), tap tempo (key 16) and the level. A tap opens their pages. |
| **SEQ tapped: NOTES** | The pattern as a piano roll: KNOB 1 walks the notes (a chord's one by one), KNOB 2 the pitch, KNOB 3 the length, KNOB 4 the force (GHOST, SOFT, NORM, HARD, ACC); PRESETS zooms (8 to 64 steps); the keys or a MIDI keyboard write the step; EDIT tapped deletes the note; OCT+ held opens its sheet (SLIDE, NUDGE, FILL, MOVE, COPY, DELETE). MENU > SEQ > STEP ADVANCE OFF keeps the cursor where it is. |
| **REC** | Records what you play into the pattern. Hold REC for about 2 seconds to clear the pattern (a ring fills; let go before and nothing is cleared). |
| **MOTION** | While REC records (and GLO > TOUCH > MOTION REC is ON, the default), the pattern also records three motion lanes: BEND (the lever and MIDI pitch bend), RIBBON and PRESS (the pressure, the keys' or MIDI's, after the AT shaping), four values a step. Each recorded note also keeps its own pressure (a short curve), played back on that note alone, so a sound like PRESS BEND bends only the note you leaned on. They play back smoothly with the pattern. Move a control over a lane and you hear your hand instead; recording again replaces only the part you moved (overdub). To clear a lane, hold EDIT and turn KNOB 4 to BEND, RIBBON, PRESS (with the notes' own pressure) or ALL, then let go of EDIT (turn it back to -- to clear nothing); EDIT + OCT- brings the lane back. LENGTH x2, SHIFT, the sections and the projects keep the lanes. With MIDI = SEQ they go out as pitch bend, CC 16 and channel pressure. |
| **KEY TOUCH** | GLO > **TOUCH** > **KEYS**: **OFF** (every key at velocity 100, the default), **VEL** (velocity from how hard you press), **V+AT** (velocity and pressure) or **AT** (pressure alone, every note at 100: steadier than velocity, since how a key reads also depends on how much of your finger is on it). Only the 20 keys from the left (F3 to C5) measure touch; the 7 at the right play at 100. Run **TOUCH CAL** once: each key gently, then firmly, three times, the bar showing the press. Pressure reaches the matrix as POLY AT (STAMPEDE's PRESS BEND uses it). |
| **LEVER** | Hold a key down and **OCT-** / **OCT+** bend the pitch down / up instead of changing the octave (with no key down they change the octave as before). GLO > **TOUCH 3** > **LEVER** sets how the bend behaves: **LATCH** (the default): it bends while you hold the button and stays where it is when you let go, even after the notes end; the same button again bends further; the other button sends it back to the middle, where it stops even if you keep holding (press again to bend the other way). **SPRNG**: it springs back to the middle when you let go (a quick tap still bends all the way first). **HOLD**: it bends while you hold the button and stays when you let go; the other button moves it back, through the middle and on to the other side for as long as you hold it. **OFF**: OCT- / OCT+ only change the octave. To move a bend that stayed, hold a key and press a button. **LTIME** is how long a full bend takes. Loading a project or NEW puts the lever back in the middle. The bend goes out on MIDI as pitch bend and is recorded in the BEND motion lane. |
| **RIBBON** | Hold a key down and turn **SELECT** to bend the pitch, one click a step (GLO > **TOUCH 3** > **RIB**: the bend at a full turn; **TOUCH 4** > **STEPS**: the clicks to get there). **TOUCH 4** > **SPRNG** sets how it comes back: **LATCH** (the default): it stays where you turn it, even after the notes end; a turn further the same way bends further; **one click back the other way** springs it back to the middle (the rest of that turn is ignored, so it stops there; pause, then turn again to bend the other way). **SPRNG**: it springs back to the middle on its own, **DELAY** after your last click, in **RETRN**. **HOLD**: it stays; turning back moves it back a click at a time and stops at the middle. Loading a project or NEW puts it back in the middle. It goes out on MIDI as CC 16 and is recorded in the RIBBON motion lane. |
| **MIDI** | GLO > **MIDI**: **IN CH** OMNI (every channel plays the synth, the default) or one channel, 1 to 16 (a sequencer keyboard or a DAW driving other gear on the other channels); **OUT** the channel the keys, arp and sequencer go out on; **BEND** the pitch bend range (AUTO, 1..24); **LOCAL** ON / OFF. GLO > **MIDI 2**: **PROG** (program changes load sounds), **SEND** (OFF, or CC: the knobs send their CCs), **BRTH** (OFF, VOL: CC 2 breath is the volume, V+AT: and the aftertouch). With MPE on, every channel plays it; in MULTI, the layer keeps its own channel. Every message: [docs/midi.md](docs/midi.md). |

> **Status:** Clipper Pro 1.3, a bug-fix release (no more distortion at the end of notes, 3.5 mm and USB; UNISON no longer stacks its voices in phase; PHYS strings in tune; NZ ARCADE, SAMPLE and GRAIN follow the top keys; a firm press on a layer button opens its pages; on REC READY a knob changes only its own value; the faders follow a real volume curve; the sustain pedal held as long as you like; no hanging note on TRS MIDI OUT; the lights no longer freeze after 25 days; the installer no longer freezes Chrome on Windows; VECTOR's filter MODEL changes without a click; SLOOP projects keep Clipper's values; backups keep your wavetables; the way back accepts M-VAVE's V16), built on Clipper 1.3, tested on the SMK-37 as 1.2. Install at your own risk (read the
> [safety notes](CLIPPER-PRO.md#safety)); your unit's calibration is never written, and M-VAVE's own updater takes you
> back at any time.

## HYBRID, Clipper's voice

| | |
| --- | --- |
| **3 oscillators** | Each one is SAW, PULSE (width), TRI, SINE (with feedback), MORPH (sine → tri → saw → square), SUPER (seven detuned saws), FM (phase-modulated by the oscillator before it), SYNC (hard sync to osc 1), FOLD (wavefolder) or NOISE (with colour). Each has coarse ±48 semitones, fine, level and shape, and can restart at each note or keep a fixed pitch. |
| **Mix** | Ring modulation of osc 1 × osc 2, analog drift (per voice and per oscillator), drive. |
| **2 filters** | LP12, LP24, BP, HP, NOTCH, PEAK. They run in series, in parallel (with a balance) or split (osc 1 into F1, osc 2 and 3 into F2). Each has an envelope amount, keyboard tracking and velocity. |
| **Modulation** | Per voice: ENV2 and ENV3 (ADSR) and LFO2 and LFO3 (6 waves, free or key-synced, tempo-synced from 1/32 to 8 bars, fade-in), on top of the sound's amplitude ADSR and LFO1. |
| **Matrix** | 16 slots of *source × via × amount → destination*. Sources: ENV2, ENV3, AENV, LFO1-3, velocity, key, mod wheel, aftertouch (channel or per note), bend, expression, a random value per note, the macros M1-M4. Destinations: pitch (all or per oscillator), shapes, levels, ring, cutoff (both or each), resonances, drive, amplitude, the LFO rates, filter balance. |
| **On the FM-1** | EDIT shows CUT, RES and FENV (offsets on both filters), PTCH (the factory patch), and M1-M4: four macros whose effect each patch decides. |
| **In the editor** | Every value above, live while you play. Patches export as `.stallion.json` and import from it or from Clipper's `.clipper.json`. |
| **27 factory patches, 32 sounds** | STALLION (Clipper's CLIPPER), POLY PAD, STRINGS, ACID, SYNC LEAD, FM BELL, FM KEYS, WOBBLE, DRIFT BASS, RAVE SIREN, BRASS, VOX, PLUCK, RISER, METAL, FOLD BASS, CHIP LEAD, GLASS PAD, DRONE, BOOM SUB, TRANCE, SAW PLUCK, SPACE BEL, INIT, THICK BASS, THICK LEAD, LADDER PAD; five more sounds play them with their own macros (CLIP WIDE, DARK PAD, ACID HARD, BRIGHT BEL, SOFT SYNC). They are level-matched with every other sound and sorted into the PRESETS list by kind. |

On Windows, if the page stops while the keyboard restarts into its update mode: close Chrome completely, open the page
again and press Install (it goes on where it stopped), or use the command line (no browser). It
does the same: `python tools/fm1_install.py build/release/`.

## Editor

Stallion's web editor (Clipper's, carried on) works with the FM-1 and the SMK-37 Pro: every parameter live, the HYBRID and FMX patches, the MOD SEQ, the tuning,
six-operator `.syx` banks, wavetables, the sequencer, backups. Open it from the installer page or with `OPEN-EDITOR.bat`.

**Without building anything**: open the [site](https://eberonltd.github.io/stallion-fm1/) in Chrome or Edge with the instrument on USB and pick its installer: the [FM-1 installer](https://eberonltd.github.io/stallion-fm1/webapp/installer/) or the [SMK-37 Pro installer](https://eberonltd.github.io/stallion-fm1/webapp/installer-smk37/). Make a backup with the [editor](https://eberonltd.github.io/stallion-fm1/webapp/editor/) first, then click Install. The firmware packages are also attached to each [release](https://github.com/EberonLtd/stallion-fm1/releases): `stallion-<version>.fwsc` for the FM-1, `stallion-smk37-<version>.fwsc` for the SMK-37 Pro.

**From the source**: on Windows, see **[QUICKSTART.md](QUICKSTART.md)** (in French: [DEMARRAGE-RAPIDE-FR.md](DEMARRAGE-RAPIDE-FR.md)): `INSTALL-STALLION.bat` builds the firmware and opens the installer at `http://localhost:8767` (SLOOP uses 8766, so the two never get mixed up). Elsewhere, see [BUILDING.md](BUILDING.md).

Projects, user presets, samples and settings stay in the same flash places as SLOOP's and Clipper's. Going from SLOOP or Clipper to Stallion keeps them; a sound on HYBRID plays another engine if you go back to SLOOP.

## Polyphony

The voices are a build option: 8 (the default, as SLOOP), 12 or 16 (`INSTALL-STALLION.bat 12`). Since 0.7 the second core renders half of a sound's voices (MENU → SYS → 2ND CORE), which is what 12 or 16 voices need. The host measurements put a typical HYBRID voice at about 2.5 % of the FM-1's CPU and the heaviest factory patch (STALLION, two SUPERs) at about 6.6 %. GLO → SYSTEM → CPU shows the real load on the device: those readings decide the default.

## Tests

`HOST_ONLY=1 tests/run_tests.sh` runs everything that does not need the JieLi toolchain (1.2: a note held under others, live, recorded, overdubbed and in a free take, on POLY and UNI2, LEN under later notes, a MIDI burst, a sample played again from silence, every engine's value names under AddressSanitizer; 1.1: the SLOOP 2.7.2 sequencer, every MIDI message, the effects sweep (a sine through every effect at its extremes); 1.0: SPREAD, the 12 slots, LFO 3, ENV 2 LOOP, the macros, HALL / SHIMMER, PING / TAPE, the stores' tail, the voices on both cores with SPREAD, and the click test of 55 cases; 0.9: the filter models, VINTAGE, the TUNING, TURNG; 0.8: PONY and its chorus, VECTOR, the MOD SEQ, SPICE / DICE, their pages; 0.7: the voices on both cores in every mode of the host double of core 1, and under ThreadSanitizer; UNDO, COMPARE, FAV, KBD HOLD): the synth mode (HOME, presets, keys, pages, one instrument, MIDI omni), the HYBRID tests (the pitch of every oscillator, the filters, the matrix, the controllers, 300 random patches, the cost per voice), the editor's commands, golden renders of every sound, ASan and UBSan, a soak test, and the web pages.

## Credits

Stallion is by Eberon Limited (John Gorman), on the work of everyone below. The full list, with each project's licence, is in **[CREDITS.md](CREDITS.md)**; the licences file by file are in [LICENSING.md](LICENSING.md).

- **[Felucca](https://github.com/hugelton/Felucca)** by Leo Kuroshita (@kurogedelic) / Hügelton Instruments: the platform, the engines, the editor and the installer.
- **The second core** (0.7): its HAL (`firmware/hal/fm1_cpu1.{h,S}`) is Charles Vestal's ([X0X](https://github.com/charlesvestal/fm1-x0x)), after Kerem Kilic's [Melodee](https://github.com/keremimo/melodee); brought to SLOOP by Andre Rezemini ([sloop-lab](https://github.com/andrerezemini/sloop-lab), `cpu1.c`, `tests/cpu1_host.c`); 2.7.1's hold with the bus traps off after Felucca 1.6.1. Clipper's voice split is its own.
- **Knob acceleration, LEVEL / PAN glide** (0.7): after [JIANT FM](https://github.com/juanjiant-bit/JIANT) by juanjiant-bit, through SLOOP 2.7.
- **[Clipper](https://github.com/isod89/clipper-fm1)** by 3dSam (isod89): everything Stallion starts from (Stallion forked it at 1.1 and merged 1.3). Its release notes are the *Clipper X.Y* sections above.
- **[SLOOP](https://github.com/isod89/sloop-fm1)** by 3dSam: everything Clipper starts from. Its manuals are in [docs/sloop](docs/sloop/).
- **RODEO**: after zvenson's [OberVave](https://github.com/zvenson/obervave) (GPL-3.0-only; the same lineage: Felucca, SLOOP, Dinghy): its voice (the oscillators, hard sync, X-MOD and its state-variable filter in six modes) ported to Clipper's engine (`firmware/src/eng_rodeo.c`), and its factory programs mapped to RODEO's presets.
- Ideas, not code: the modelled engines (PONY, PALOMINO, STAMPEDE, RODEO, MARE), MACRO's four macro-oscillators, the filter models (the Sallen-Key, state-variable and diode-ladder designs), TURNG (a Turing machine: a looping, mutating shift register), SPICE / DICE (seeded variations of the notes); `.scl` is Manuel Op de Coul's Scala format. All of it is Clipper's and Stallion's own code.
- **KEY TOUCH** (Stallion: velocity and pressure from the FM-1's own keys, GLO > TOUCH): the sensing, the state machine, the learning, the calibration and the pressure from [ESTAMPIE](https://github.com/ocean/estampie) by Drew Robinson (GPL-3.0-only, `firmware/src/key_touch.c`), which ported them from Charles Vestal's [Velo](https://github.com/charlesvestal/fm1-velocity) and [MONK-1](https://github.com/charlesvestal/fm1-monk) (GPL-3.0-only): see [LICENSING.md](LICENSING.md).
- **LEVER** and **RIBBON** (Stallion: OCT- / OCT+ as a bend lever, sprung, held or latched, and SELECT as a ribbon, latched, sprung or held, while a key plays a note, GLO > TOUCH): Stallion's own code (`firmware/src/touch_ctl.c`), after the behaviour of FuMi-1's rocker (the lever), CS-1's ribbon (its spring, delay and hold) and Penumbra (a held key gating the bend); no code taken from them.
- The supersaw follows Adam Szabo's analysis of it (*How to Emulate the Super Saw*, 2010). The FM6 engine is msfa (Dexed), and PHYS comes from DaisySP and Mutable Instruments Rings: see [LICENSING.md](LICENSING.md).

## Trademarks

M-VAVE, FM-1 and SMK-37 are trademarks of their owners, and any other product or company name is a trademark of its owner. Stallion is independent firmware: it is not affiliated with or endorsed by any of them, nor by M-VAVE. No code, sample, factory patch or manual text of any instrument maker is in Stallion.
