# Stallion 1.0.0

Stallion 1.0.0 is the first release of Stallion, free, open-source synthesizer firmware for the **M-VAVE FM-1** and the **M-VAVE SMK-37 Pro**, by Eberon Limited (John Gorman).

Stallion is built on [Clipper](https://github.com/isod89/clipper-fm1) by 3dSam (isod89) and Clipper Pro, its SMK-37 port (isod89 with M. Geisler). Clipper grew out of [SLOOP](https://github.com/isod89/sloop-fm1), and all of them are based on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita / Hügelton Instruments. Stallion keeps everything they do. These notes list what Stallion 1.0.0 adds on top: **131 new features and fixes**.

26 engines and 819 factory sounds, 4 of the engines new (PALOMINO, STAMPEDE, MARE, and RODEO after zvenson's OberVave) and Clipper's PONY rebuilt (24 engine and sound features) · 6 filter features · 2 effect features · 12 modulation and matrix features · 10 playing features · 11 touch and aftertouch features · 8 sequencer and arpeggiator features · 9 screen and control features · 14 web editor features · 5 saving features · 3 MIDI features · 2 installing features · 25 fixes.

## Licence

Every part of Stallion is free to use, change and share under its listed licence: the code under GPL-3.0-only (the few third-party source files under the GPL-compatible licences listed in `LICENSING.md`), the data (samples, fonts, images) under the licences listed file by file in `assets/MANIFEST`. Nothing in Stallion is "all rights reserved".

## Install

- Open [eberonltd.github.io/stallion-fm1](https://eberonltd.github.io/stallion-fm1/) in Chrome or Edge on a computer, or Chrome on Android (not Safari, iPhone or iPad), and press **Install on FM-1** or **Install on SMK-37 Pro**.
- Packages: `stallion-1.0.fwsc` for the FM-1, `stallion-smk37-1.0.fwsc` for the SMK-37 Pro. Stallion supports the SMK-37 Pro only: the installer refuses the SMK-37 Elite, the MKE-P37 and the Starrykey 37 Play.
- Save a backup with the editor (the Backup tab) before you install over Stallion, Clipper or SLOOP.
- On macOS, if the instrument's MIDI port still shows as *Clipper* or *Hügelton Instruments - Clipper*: unplug it, delete that device in Audio MIDI Setup > MIDI Studio, and plug it in again.
- The guide: [GUIDE.md](../GUIDE.md).

## What is new

**819 sounds on 26 engines.** Every engine now comes with a full bank of its classic sounds.

- **A full bank for every engine**: 32 classic sounds on 23 engines, 28 on ACID and SAMPLE, 27 on WSEQ (819 in all), every bank starting with a plain INIT.
- **Centred and level-matched** (Clipper's level-matching, extended): every factory sound has its left and right within 1 dB, and each is measured with a phrase that suits it and trimmed to the same loudness (within 1.5 dB; the sample-based SAMPLE and GRAIN sounds keep their own levels).
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

### Engines and sounds (24 changes)

- **PALOMINO**: a big early-1980s 8-voice poly. Two VCOs with six waves each, cross mod, hard sync, an OTA filter at 12 or 24 dB. 32 sounds: pads, strings, brass, leads, basses and keys.
- **STAMPEDE**: a late-1970s poly with two complete channels per voice. A wave morph through noise, saw, pulse, sine and ring; channel II's feet and detune; a resonant high-pass then low-pass with its own envelope. 32 sounds.
- **RODEO**: the punchy, brassy 8-voice polys of 1979-84, ported from zvenson's OberVave. Two oscillators with hard sync and X-MOD, and a filter in six modes (LP12, HP12, BP12, NOTCH, LP24, MORPH). 32 sounds.
- **MARE**: a semi-modular paraphonic voice with two VCOs and two ladder filters. The filters sit apart in stereo, side by side (high-pass and low-pass) or in series. 32 sounds: basses, leads, pads, sequences, drones, keys, strings and brass.
- **PALOMINO's extra waves**: sine and noise on VCO 1, triangle and square on VCO 2, as on the modern reissue of the instrument it follows.
- **PALOMINO VCO-2 LOW**: VCO 2 drops eight octaves to become a slow modulator for wobbles and slow vibrato.
- **P>VCO** on PALOMINO and MARE (MOD page): send the LFO and envelope pitch modulation to both VCOs or just one, for sync sweeps and beating. On MARE it is the instrument's PITCH MOD ASSIGN.
- **MARE FILTER FM and RING** (EDIT 3): VCO 2 sweeps both filters at audio rate, or rings against VCO 1, for growls and metallic edges.
- **MARE split modes** (MODE SPLT, SPL+N): VCO 1 alone goes to the left filter and VCO 2 alone to the right.
- **MARE's normals**: a cable into VCO 1's pitch moves VCO 2 too, and filter 2 follows filter 1's cutoff, until you give them cables of their own.
- **STALLION**: a new signature HYBRID sound.
- **Every factory sound level-matched and centred** (Clipper's level-matching, extended): no sound jumps out louder or quieter than the next, and none sits in one ear (left and right within 1 dB). The test suite checks both.
- **PALOMINO VCO-2 fine tune** (EDIT > VOICE 2 > DTUNE): sets how far VCO 2 sits above VCO 1, from 0 to about 16 cents (the old fixed 5 cents is the default, 40), for thick brass, strings and pads.
- **Resonance that stops short of howling**: PALOMINO's filter now stops just below self-oscillation at full RES, and RODEO's up to RES 123, so a resonant bass or brass rings and decays instead of whistling. RODEO's top four steps sing on purpose, as its originals do.
- **PALOMINO pulse width one way**: SHAPE (and an LFO or envelope on it) sweeps the pulse from square towards narrow, once per cycle, instead of folding both ways.
- **An analog attack** on PALOMINO, STAMPEDE, RODEO and MARE: the attack charges quickly and then eases in, the same length as before, for swells and brass with more bite.
- **POLY2** (EDIT > VOICE 2 > ALLOC): a new note cuts the notes already let go, so only the last chord keeps its release. Good for sequences and arpeggios.
- **Free delay times** (FX > DELAY > TIME): 35, 50, 75, 100, 150, 200, 300, 400, 500, 600, 700 and 780 ms, besides the tempo values, on every sound.
- **MARE SPACE and DIST**: SPACE works as the instrument's SPACING: it moves VCF 1 alone (the left low-pass, or the high-pass in PARA and SERI), clockwise up, while VCF 2 stays at CUT. DIST drives the two ladder filters, so it thickens the sound before the filters instead of coming after them.
- **STAMPEDE plain SINE after the filters**: the SINE wave ignores the filters, as a plain sine channel should. IL can go below zero, so a note can start under the cutoffs as set.
- **STAMPEDE filter envelope with its own times**: as on the original, the filter envelope has its own attack, decay and release, set with ENV 2's ATK, DEC and REL (ENV 2 ATK 0 keeps the volume envelope's). The brass sounds now open slowly over a third of a second or more and settle smoothly.
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
- **PONY's CHAR** (EDIT 3): the original instrument's quirks, each a switch: CLEAN (none), CLASSIC (the default: STEPS off, the chorus's hiss, the high-pass after the voices with its bass boost, the wheel's vibrato from the LFO, a 1 ms GATE, darker noise), FULL (also the amp's fast tails, a free-running LFO with a waiting delay, coarser cutoff steps and a 4.2 ms processor tick). The web editor has a box for each.
- **PONY's chorus, measured from the original**: I 0.53 Hz, II 0.87 Hz, each side's delay its own, the delayed sound louder than the dry: wider, and about 4 dB louder with it on, as on the original. I+II is PONY's own (the original has no third mode). The INSERT's chorus types are the same chorus.
- **WHISTLE**: a new PONY sound in CLAV's place, the filter whistling on its own.
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
- **The MATRIX page** (Clipper's, extended): the selected cable in large type (for example LFO1 > CUT +62), coloured by signal type. Sources and destinations are in panel order, and MARE's normals are shown in words.
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
- **Live controls** (Clipper's, extended): values are sent while a control moves, not only when you let go.
- **Envelope grab points**: drag the envelope picture's attack, decay, sustain and release points with a mouse, finger or pen.
- **The patch bay**, on every engine: drag cables between 64 jacks to patch the matrix, coloured as on the device, in large, bright type. Switch to the MATRIX tab to see the same cables as a table; up to 24 cables per sound, and only the inputs the engine uses. The matrix is shown once: the old card of MATRIX knobs is gone. FILTER FM, RING, SPLT/SPL+N and the VCO1->VCO2 pitch normal are MARE's own.
- **MIDI 3 settings** (CC A, CC B, SUST) and the new INSERT types are named in the editor.
- **Tuning card** (Clipper's, extended): checked against the device, and it says what the FM-1 tunes to.
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

### Fixes (25)

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

## Credits

Everyone whose work Stallion uses is named in [CREDITS.md](../CREDITS.md), with every licence in [LICENSING.md](../LICENSING.md).

M-VAVE, FM-1 and SMK-37 are trademarks of their owners. Stallion is not affiliated with or endorsed by them. No warranty: you install it at your own risk.

Copyright (C) 2026 Eberon Limited (John Gorman)
