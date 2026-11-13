#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Host tests (no hardware). Run from the repo root after ./build.sh:
#   tests/run_tests.sh
#
# Regression suite (tests/regress.c, tests/target_budget.py; details at the top of regress.c):
#   golden renders  every engine x preset, the drum kit, voice modes, FX sends, a 4-track mix: one hash
#                   each in tests/golden.txt. A change of the sound fails with the list of renders.
#   health          clipping, DC, peak level, voices free after the release, silence at the end.
#   CPU             cost / sample per preset and mix, relative to the idle + drums mix (tests/cpu_baseline.txt,
#                   +25 %; counted by the kernel or under callgrind, else timed at +35 %), ns printed;
#                   target: loop instructions of the render functions in build/felucca.dis
#                   (tests/target_budget.txt, +10 %; exact, static).
#   voices          the budget of 8, steal fades, MONO / LEGATO / UNISON keep their note, the VOICE cap,
#                   no hanging notes on any MIDI / key routing.
# FM6 (tests/fm6_test.c): the 6-operator FM engine (firmware/src/eng_fm6.c, fm6_core.c, fm6_bank.c): the 32
#                   algorithms' carriers, the operator envelopes ending the voice, retrigger, DC / clipping, the
#                   eight macros, the patch formats (packed, SysEx), the 6-voice cap, the patch bank on a
#                   simulated NOR; demos in build/fm6_demo/.
# After an intended change of the sound: GOLDEN_UPDATE=1 sh tests/run_tests.sh, review the diff
# of tests/golden.txt, commit it with the change. After an intended change of the cost (or a new
# compiler): BUDGET_UPDATE=1 (rewrites cpu_baseline.txt and target_budget.txt). VERBOSE=1: every render.
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
# SLOOP's groovebox tests (four tracks, drums, song) build with CLIPPER_SYNTH=0; Clipper's own synth mode
# (CLIPPER_SYNTH=1, the firmware's default) has its tests in the section "Clipper synth" below
CCS="${CC:-cc} -O1 -Wall -Wno-unused-function"
CC="$CCS -DCLIPPER_SYNTH=0"
fail=0
run() { echo "== $1"; local n="$1" rc=0; shift; "$@" || rc=$?; [ $rc -eq 0 ] || { fail=1; echo "FAIL: $n (exit $rc)"; }; }   # a test that fails only on stderr or by its exit code still shows a FAIL line

# HOST_ONLY=1: without the JieLi toolchain (no build/felucca.fwsc): the generated headers are made here and the
# tests that need the built package (update entry, update loader, target cost of the render loops) are skipped
if [ -n "$HOST_ONLY" ]; then
    mkdir -p build/gen
    for g in font:felucca_font icons:felucca_icons samples:felucca_samples drumkits:felucca_drumkits logo:sloop_logo; do
        [ -f "build/gen/${g#*:}.h" ] || python3 "tools/gen_${g%%:*}.py" "build/gen/${g#*:}.h" >/dev/null
    done
else
    [ -f build/felucca.fwsc ] || { echo "run ./build.sh first (or HOST_ONLY=1)"; exit 1; }
fi
# the generated headers the FM6 engine needs (tools/build.py generate() makes them too; no Pillow needed)
mkdir -p build/gen
[ build/gen/felucca_tables.h -nt tools/gen_tables.py ] || python3 tools/gen_tables.py build/gen/felucca_tables.h
[ build/gen/felucca_fm6.h -nt tools/gen_fm6_patches.py ] || python3 tools/gen_fm6_patches.py build/gen/felucca_fm6.h
python3 tools/gen_wavetables.py build/gen/felucca_wavetables.h >/dev/null   # (cached: only when the script changed)

# Stallion R51: the icon atlas is what tools/draw_icons.py draws (its origin: that GPL code); needs Pillow
if python3 -c "import PIL" 2>/dev/null; then
    run "icon atlas: assets/icons.png drawn by tools/draw_icons.py" python3 tools/draw_icons.py --check
    run "Stallion logo: assets/logo and the web icons drawn by tools/gen_logo.py (R1)" python3 tools/gen_logo.py --check
else
    echo "== skip icon atlas check (no Pillow)"
fi

$CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"
# Stallion: MENU > SYS > FACTORY RESET erases the user objects only (the settings rewritten with the panel table), non-current copy first, on either board's flash map
$CC -Ifirmware/hal -o "$OUT/factory_reset_test" tests/factory_reset_test.c
run "factory reset (FM-1): every user object erased (non-current copy first: a power cut never brings an older record back), the settings rewritten, every other byte as it was" "$OUT/factory_reset_test"
$CC -DBOARD_SMK37=1 -Ifirmware/hal -o "$OUT/factory_reset_test_smk" tests/factory_reset_test.c
run "factory reset (SMK-37): the same, never the head, app, staging, BTIF / USRTRIM, samples, stock, key_mac" "$OUT/factory_reset_test_smk"

$CC -o "$OUT/recovery_test" tests/recovery_test.c
run "application USB recovery and boot-loop guard" "$OUT/recovery_test"

$CC -o "$OUT/arranger_test" tests/arranger_test.c
run "song order, timing, repeats and missing scenes" "$OUT/arranger_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/song_audio_test" tests/song_audio_test.c -lm
run "song: four simultaneous tracks, scene transition and stop" "$OUT/song_audio_test" "$OUT/song-demo.wav"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/song_ui_test" tests/song_ui_test.c -lm
run "song screen: commands, load (OCT+ twice), display bounds" "$OUT/song_ui_test" "$OUT/song-screen.ppm"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/song_arm_test" tests/song_arm_test.c -lm
run "song screen: a confirmation left unconfirmed asks again weeks later (no load / replace on one press)" "$OUT/song_arm_test"

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/studio_drums_test" tests/studio_drums_test.c -lm
run "drum lanes, kit audio, metronome, record arm, free take" "$OUT/studio_drums_test" "$OUT/drum-styles.wav"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/seq2_test" tests/seq2_test.c -lm
run "sequencer 2.0: no drift, ratchets, roll, erase / undo, ghost / hard, chords, mute / solo, nudge, locks" "$OUT/seq2_test"
# (Stallion 1.0) ratchets up to x8 on a synth note: exact hits, x1..x4 on their old blocks, swing, nudge, 240 BPM, the bits, a project round trip
$CCS -O2 -w -DCLIPPER_SYNTH=1 -Ibuild/gen -Ifirmware/src -o "$OUT/ratchet8_test" tests/ratchet8_test.c -lm
run "ratchet x1..x8: a note x h plays h even hits in its step (x1..x4 as before) at any swing or nudge, up to 240 BPM on 1/32; x5..x8 saved and loaded" "$OUT/ratchet8_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/hold_rec_test" tests/hold_rec_test.c -lm
run "2.8.3: a note held under others (live, recorded, overdubbed, free take), MIDI in overflow" "$OUT/hold_rec_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/len_test" tests/len_test.c -lm
run "2.8.3: SEQ > NOTES, LEN under later notes, MONO / STEP FULL, shorter, delete" "$OUT/len_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/drum_steal_test" tests/drum_steal_test.c -lm
run "2.8.3: drum voices on a busy beat, every kit: no kick cut by another sound" "$OUT/drum_steal_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/chop_attack_test" tests/chop_attack_test.c -lm
run "2.8.3: a sample played again starts from silence with its ATK (POLY same note, MONO, LEGATO, UNISON), no click" "$OUT/chop_attack_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/fade_noise_test" tests/fade_noise_test.c -lm
run "SLOOP Pro #4: a note fading out keeps 16-bit resolution (DAC, MASTER 2048, USB FULL, 808 SLIDE): no 16 LSB floor" "$OUT/fade_noise_test"

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/drumkit_test" tests/drumkit_test.c -lm
run "synthesised drum kits: every kit x sound bounded, audible, finite, levels, cost" "$OUT/drumkit_test" "$OUT/drum-kits.wav" "$OUT/drum-kits.txt"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/userkit_test" tests/userkit_test.c -lm
run "user drum kits (KIT USR1..USR3): a user slot's sounds on the drum lanes" "$OUT/userkit_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/dsyn_test" tests/dsyn_test.c -lm
run "SYN1..SYN4 drum kits and the editor's DRUM SYNTH commands (v10)" "$OUT/dsyn_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/punch_test" tests/punch_test.c -lm
run "punch-in FX: 16 effects, bounded, dry after release, FX-held keys" "$OUT/punch_test" "$OUT/punch-fx.wav"

$CC -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/ui_pages_test" tests/ui_pages_test.c -lm
run "live UI: pages, layers (punch, steps, erase, roll, key, mix), holds, drums, REC, fuzz" "$OUT/ui_pages_test" "$OUT"
$CC -o "$OUT/smk_wheel_test" tests/smk_wheel_test.c
run "SMK-37 Pro pitch wheel: the bend is back to 0 at rest, however slowly the wheel was moved" "$OUT/smk_wheel_test"
$CC -w -DBOARD_SMK37=1 -Ifirmware/hal -Ifirmware/src -Ibuild/gen -o "$OUT/smk_stock_test" tests/smk_stock_test.c
run "SMK-37 the way back to M-Vave: the stock mapping's marker released before the first USRFLASH erase, once" "$OUT/smk_stock_test"
# every erase of the app goes through fl_erase4k_quiet (felucca.c), whose first step on the SMK-37 is st_stock_touch
stock_hooked() {
    ! grep -n 'fl_erase4k_ram' firmware/src/*.c | grep -v '^firmware/src/felucca.c' &&
    grep -q 'st_stock_touch(off);' firmware/src/felucca.c && echo hooked
}
run "SMK-37 every app erase goes through the stock marker's hook" stock_hooked
$CC -Ifirmware/src -o "$OUT/smk_pedal_test" tests/smk_pedal_test.c
run "Clipper Pro sustain pedal: polarity learnt at power-on, bounce, an open jack, never stuck" "$OUT/smk_pedal_test"
$CC -o "$OUT/smk_midi_out_test" tests/smk_midi_out_test.c
run "SMK-37 TRS MIDI OUT: the bytes, the ring, one byte at a time, no UART: no hang, no note left on" "$OUT/smk_midi_out_test"
$CC -Ifirmware/src -o "$OUT/smk_midi_out_flush_test" tests/smk_midi_out_flush_test.c
run "SMK-37 TRS MIDI OUT: a note-off dropped during a flush: the flush starts again, no note left on" "$OUT/smk_midi_out_flush_test"
$CC -O2 -w -Ifirmware/hal -o "$OUT/smk_pad_test" tests/smk_pad_test.c -lm
run "SMK-37 pads: hits at once at their peak, no rebound hits, rolls, steady holds, the same velocity wherever the readings fall" "$OUT/smk_pad_test"
# Clipper Pro: the SMK-37 build compiles on the host with every warning (the board's own code included), and its
# wheels, pedal, faders and pads go through the part controls and the CC map a MIDI keyboard uses
smk_build() {
    ${CC_SMK:-cc} -fsyntax-only -Wall -Werror -Wno-unused-function -DBOARD_SMK37=1 -Ibuild/gen -Ifirmware/src -Ifirmware/hal \
        firmware/src/felucca.c 2>/dev/null || clang -target i386-unknown-linux-gnu -fsyntax-only -ffreestanding -nostdinc \
        -isystem "$(clang -print-resource-dir 2>/dev/null)/include" -Wall -Werror -Wno-unused-function -DBOARD_SMK37=1 \
        -Ibuild/gen -Ifirmware/src -Ifirmware/hal firmware/src/felucca.c || return 1
    grep -q 'part_ctl_cc(&trk\[0\], 64u' firmware/src/seq.c && grep -q 'midi_cc(t, cc, v)' firmware/src/ui_fbank.c &&
    grep -q 'smk_perf_block();' firmware/src/seq.c && echo built
}
run "Clipper Pro: the SMK-37 build compiles (-Wall -Werror); wheels, pedal and faders as MIDI controls" smk_build
# no divide by 0 (the FM-1 runs with the div0 trap off, hal/fm1_irq.h: a real one would give a wrong value
# silently): the UI fuzz, the sequencer, the projects and a minute of random live use, with UBSan
UBSAN="${CC_UB:-cc} -DCLIPPER_SYNTH=0 -O1 -w -fsanitize=integer-divide-by-zero -fno-sanitize-recover=integer-divide-by-zero -Ibuild/gen -Ifirmware/src -Ifirmware/hal"
$UBSAN -o "$OUT/ui_pages_ub" tests/ui_pages_test.c -lm && $UBSAN -o "$OUT/seq2_ub" tests/seq2_test.c -lm &&
    $UBSAN -o "$OUT/project_ub" tests/project_test.c -lm && $UBSAN -o "$OUT/soak_ub" tests/soak_test.c -lm || fail=1
mkdir -p "$OUT/ub"
run "no divide by zero (UBSan): UI fuzz, sequencer, projects, a minute of live use" \
    sh -c "'$OUT/ui_pages_ub' '$OUT/ub' >/dev/null && '$OUT/seq2_ub' >/dev/null && '$OUT/project_ub' >/dev/null && '$OUT/soak_ub' 1 >/dev/null && echo 'no divide by zero'"

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/soak_test" tests/soak_test.c -lm
run "soak: ${SOAK_MIN:-10} minutes of random live use (bounded, no hanging voices, idle after stop)" "$OUT/soak_test" "${SOAK_MIN:-10}"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/font_test" tests/font_test.c
run "the screen font at 1 bit a pixel: every glyph, the same pixels as before" "$OUT/font_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/stress_test" tests/stress_test.c -lm
run "stress (2.4): FONT_L = FONT_S at 2x, hard random use of every 2.4 addition, clean stop" "$OUT/stress_test" "${STRESS_FRAMES:-40000}"
# the same under AddressSanitizer + UBSan: any read or write out of bounds stops it (left shifts of negative values
# and the FM6 phase's wrap-around are left out: the DSP's two's complement idioms, as every compiler builds them)
ASAN="${CC_UB:-cc} -DCLIPPER_SYNTH=0 -O1 -g -w -fsanitize=address,undefined -fno-sanitize=shift-base,signed-integer-overflow -fno-sanitize-recover=all -Ibuild/gen -Ifirmware/src -Ifirmware/hal"
if $ASAN -o "$OUT/stress_asan" tests/stress_test.c -lm 2>/dev/null; then
    run "stress under ASan + UBSan (no access out of bounds)" "$OUT/stress_asan" "${STRESS_ASAN_FRAMES:-15000}" 7
else
    echo "(stress under ASan: this compiler has no AddressSanitizer, skipped)"
fi

# Clipper synth (CLIPPER_SYNTH=1, what the FM-1 runs): one instrument, HOME = the sound, no tracks / drums / song
SYN="$CCS -O2 -w -DCLIPPER_SYNTH=1 -Ibuild/gen -Ifirmware/src -Ifirmware/hal"
mkdir -p "$OUT/clipper"
$CC -o "$OUT/smk_press_test" tests/smk_press_test.c && run "SMK-37 pad pressure: dead zone, fast rise, no numbing" "$OUT/smk_press_test"
$SYN -o "$OUT/clipper_ui_test" tests/clipper_ui_test.c -lm
run "Clipper synth: HOME, presets, keys, pages, one track, MIDI omni, no drums / song" "$OUT/clipper_ui_test" "$OUT/clipper"
$SYN -o "$OUT/clipper_hold_test" tests/hold_rec_test.c -lm
run "Clipper synth (2.8.3): a note held under others, POLY and UNI2, live / recorded / overdub / free take, MIDI burst" "$OUT/clipper_hold_test"
$SYN -o "$OUT/knob_walk_test" tests/knob_walk_test.c -lm
run "Clipper Pro on the SMK-37 (SLOOP Pro #6): every page, HOME, REC READY: a knob turned alone changes one value" "$OUT/knob_walk_test" "$OUT"
$SYN -Itests -o "$OUT/gaps_test" tests/gaps_test.c -lm -lpthread
run "The engines against their originals: fine tune, RC attack, POLY2, free delay, SPACE, drive, SINE" "$OUT/gaps_test"
$SYN -o "$OUT/at_pop_test" tests/at_pop_test.c -lm
run "AT PRESSURE bar: in the gap under the boxes, never a row of the page, only what moved" "$OUT/at_pop_test"
fm1_popup_build() {                                # the FM-1 with the popup on (AT_POPUP: on both boards) compiles
    clang -target i386-unknown-linux-gnu -fsyntax-only -ffreestanding -nostdinc -isystem "$(clang -print-resource-dir 2>/dev/null)/include" \
        -Wall -Werror -Wno-unused-function -DAT_POPUP=1 -Ibuild/gen -Ifirmware/src -Ifirmware/hal firmware/src/felucca.c
}
run "AT PRESSURE popup: the FM-1 compiles with it" fm1_popup_build
$SYN -o "$OUT/fader_level_test" tests/fader_level_test.c -lm
run "Clipper Pro FADER BANK D (SLOOP Pro #7): LEVEL by GM's law, silent at the bottom, no mute in the travel" "$OUT/fader_level_test" "$OUT"
$SYN -o "$OUT/clipper_chop_test" tests/chop_attack_test.c -lm
run "Clipper synth (2.8.3): a sample played again starts from silence with its ATK, no click" "$OUT/clipper_chop_test"
$SYN -o "$OUT/clipper_fade_test" tests/fade_noise_test.c -lm
run "Clipper synth (SLOOP Pro #4): a note fading out keeps 16-bit resolution: the mix and MASTER rounded, no 16 LSB floor" "$OUT/clipper_fade_test"
$SYN -o "$OUT/clipper_stress" tests/stress_test.c -lm
run "Clipper synth: stress (hard random use, clean stop)" "$OUT/clipper_stress" "${STRESS_FRAMES:-40000}"
$SYN -o "$OUT/clipper_soak" tests/soak_test.c -lm
run "Clipper synth: soak, ${SOAK_MIN:-10} minutes of random live use" "$OUT/clipper_soak" "${SOAK_MIN:-10}"
$SYN -o "$OUT/filter_test" tests/filter_test.c -lm
run "Clipper 0.2 filters: the MOOG ladder (response, resonance, no dead band, bounded), soft SVF states, ANALOG MODL" "$OUT/filter_test"
$SYN -o "$OUT/ota_cascade_test" tests/ota_cascade_test.c -lm
run "the OTA filter (the 4-pole OTA filter chip: response, 12 dB tap, the bass thinning with RES, self-oscillation, bounded), PONY on it (the sub locked), MODL OTA" "$OUT/ota_cascade_test"
$SYN -o "$OUT/pony_test" tests/pony_test.c -lm
run "PONY's panel: the defaults as before (every preset), RANGE an octave exactly, WAVE OFF (the sub alone), KYBD 0 / old / full, MODL LADDR = Clipper's filter, VCA GATE (on, off, no click), STEPS ON as before / OFF glides" "$OUT/pony_test"
$SYN -o "$OUT/palomino_test" tests/palomino_test.c -lm
run "PALOMINO (presets, tune, VCO-2, MIX, XMOD sidebands, SYNC at VCO-1's pitch and band-limited, 12 / 24 dB, the six waves on both VCOs, VCO-2 LOW, P>VCO, bounded)" "$OUT/palomino_test"
$SYN -o "$OUT/bend_layer_test" tests/bend_layer_test.c -lm
run "Stallion BEND page and touch routing: every preset under a MIDI bend as before (tests/bend_golden.txt), RANGE UP / DOWN, the lever and ribbon, VCO1 / VCO2, VCF BEND, LEAD, the matrix's BEND POLAT CHNAT RIBBN and PIT1 PIT2 XMOD HPF (sources <= 32, destinations <= 64), user presets, projects, MPE" "$OUT/bend_layer_test"
$SYN -o "$OUT/filter2_test" tests/filter2_test.c -lm
run "FX > FILTER > SLOPE and FILTER 2: every preset as before at 24 and FILTER 2 OFF (tests/filter2_golden.txt), LADDR OTA DIODE at 12 / 24 / 48 dB an octave, bounded, SER / PAR / SPLIT, beside the filter and after the voice, LINK, the matrix's CUT2 / RES2, the duck, projects and user presets" "$OUT/filter2_test"
$SYN -o "$OUT/stampede_test" tests/stampede_test.c -lm
run "STAMPEDE (presets, tune, FEET, BAL, the HPF -> LPF band, DETUNE beating, RING sum / difference, the IL envelope, bounded, cost a voice)" "$OUT/stampede_test"
$SYN -o "$OUT/rodeo_test" tests/rodeo_test.c -lm
run "RODEO (after OberVave: presets, tune, OSC 2, DTUNE, the six filter modes' slopes, the 12 dB state-variable morph, SYNC, XMOD, bounded, cost; EDIT 3 KYBD: 86 % the old tracking sample for sample, 0 none, 100 full; PW0 widths; the factory values, kept by projects and user presets; RES 124..127 sings in every mode, bounded)" "$OUT/rodeo_test"
$SYN -o "$OUT/mare_test" tests/mare_test.c -lm
run "MARE (MARE, two VCOs and two ladders: presets, tune, WAVE, DTUNE, SYNC, FM, the ladders, SPACE left / right, PARA, SERI, noise, clean starts, bounded, unison, stores, cost)" "$OUT/mare_test"
$SYN -o "$OUT/alias_test" tests/alias_test.c -lm
run "Clipper 0.2 anti-aliasing: every oscillator's folded partials below 15 kHz (TRI, SYNC, FOLD, FM, feedback)" "$OUT/alias_test"
$SYN -o "$OUT/reverb_test" tests/reverb_test.c -lm
run "Clipper 0.2 plate reverb: decay, damping, width, level, pre-delay, stable, type switch" "$OUT/reverb_test" "$OUT/clipper/reverbs.wav"
$SYN -o "$OUT/wave_test" tests/wave_test.c -lm
mkdir -p build/wave_demo
run "Clipper 0.2 WAVE: band-limited tables (no aliasing), building, POS, DTN / SUB, user tables, bounded, demos" "$OUT/wave_test" build/wave_demo
SYN_UB="${CC_UB:-cc} -DCLIPPER_SYNTH=1 -O1 -g -w -fsanitize=address,undefined -fno-sanitize=shift-base,signed-integer-overflow -fno-sanitize-recover=all -Ibuild/gen -Ifirmware/src -Ifirmware/hal"
$SYN -o "$OUT/engines2_test" tests/engines2_test.c -lm
mkdir -p build/eng2_demo
run "Clipper 0.2 ACID (accent, sweep, slide), STRINGS (registers, ensemble), MACRO (chords, swarm, additive, speech, LPG)" "$OUT/engines2_test" build/eng2_demo
mkdir -p "$OUT/synth3"
$SYN -o "$OUT/synth3_test" tests/synth3_test.c -lm
run "Clipper 0.3: master EQ and COMP, poly unison, VEL / MW / AT, LAYER and SPLIT, SWAP, MORPH, RANDOM, the pages (0.4: MATRIX, LFO 2, ENV 2)" "$OUT/synth3_test" "$OUT/synth3"
if $SYN_UB -o "$OUT/synth3_ub" tests/synth3_test.c -lm 2>/dev/null; then
    mkdir -p "$OUT/synth3_asan"
    run "Clipper 0.3 under ASan + UBSan" sh -c "'$OUT/synth3_ub' '$OUT/synth3_asan' >/dev/null && echo clean"
fi
$SYN -o "$OUT/synth4_test" tests/synth4_test.c -lm
run "Clipper 0.4: the matrix on every engine (LFO 2, ENV 2, VEL, KEY, RAND, MW, AT, SLIDE), MPE, projects, user presets" "$OUT/synth4_test"
if $SYN_UB -o "$OUT/synth4_ub" tests/synth4_test.c -lm 2>/dev/null; then
    run "Clipper 0.4 under ASan + UBSan" sh -c "'$OUT/synth4_ub' >/dev/null && echo clean"
fi
$SYN -o "$OUT/matrix_trig_test" tests/matrix_trig_test.c -lm
run "Stallion matrix patching: GATE, E2END, TRIG / SYNC inputs, CC A / CC B, SUSTAIN PATCH" "$OUT/matrix_trig_test"
mkdir -p build/eng3_demo
$SYN -o "$OUT/synth6_test" tests/synth6_test.c -lm
run "Clipper 0.6: an INSERT per sound (bypass bit for bit, every type, fades, tail, the layer's own, projects, user presets, SLOOP 2.6's), MULTI (the layer on its own channel)" "$OUT/synth6_test"
if $SYN_UB -o "$OUT/synth6_ub" tests/synth6_test.c -lm 2>/dev/null; then
    run "Clipper 0.6 under ASan + UBSan" sh -c "'$OUT/synth6_ub' >/dev/null && echo clean"
fi
$SYN -o "$OUT/fmx_test" tests/fmx_test.c -lm
mkdir -p build/fmx_demo
run "Clipper 0.6 FMX: presets in tune, waves, PM / FM / RING, routing, 40 templates, PEG, bank (FMX and DX7 slots), DX7 converted" "$OUT/fmx_test" build/fmx_demo
$SYN -o "$OUT/noise_clock_test" tests/noise_clock_test.c -lm
run "NOISE LFSR: the clock follows the keys past the sample rate (NZ ARCADE, clipper-fm1 #10)" "$OUT/noise_clock_test"
$SYN -o "$OUT/sample_pitch_test" tests/sample_pitch_test.c -lm
run "SAMPLE, GRAIN: the pitch follows the keys to the top of the keyboard (up to the decode bound)" "$OUT/sample_pitch_test"
$SYN -o "$OUT/grain_release_test" tests/grain_release_test.c -lm
run "GRAIN: a note played in the release of the one before is not starved of grains" "$OUT/grain_release_test"
$SYN -o "$OUT/phys_tune_test" tests/phys_tune_test.c -lm
run "PHYS: every pitched preset in tune (+-5 cents; a curved bridge +-20), notes 28..100" "$OUT/phys_tune_test"
$SYN -o "$OUT/unison_level_test" tests/unison_level_test.c -lm
run "UNISON, UNI4: every engine spreads its voices' phases (no stack of voices in phase over POLY)" "$OUT/unison_level_test"
$SYN -o "$OUT/tail_level_test" tests/tail_level_test.c -lm
run "Stallion: a note over a quiet release tail (below -42 dB) as loud as from silence: ANALOG PALOMINO STAMPEDE RODEO MARE, every preset, POLY (UNI4 shown)" "$OUT/tail_level_test"
$SYN -o "$OUT/quiet_steal_test" tests/quiet_steal_test.c -lm
run "Stallion: voices given up on PONY PAD's long release (chords played live, the SMK-37's load; twice as fast, 8 voices; with a second part): loud tails fade out over >= 5 ms (the part's own for a note too), quietest first (tied: the oldest, FM6), no held note while a tail sounds, no tail lost the load did not need, a taken voice's note from silence, one victim a note-on, onsets not delayed while a voice is free, a held 8-note chord at 134 % no more late halves than main, no waiting note after all-off, an engine switch, MONO" "$OUT/quiet_steal_test"
# Stallion: every factory sound of the synth as loud as its target (tools/level_presets.py, the ear-weighted BS.1770 of its
# phrase before the master's limiter; ~25 s of CPU, every preset in a process of its own, on every core): a sound more
# than 1.5 dB off fails with the worst ones (re-level: the docstring of tools/level_presets.py; a new preset: its letter
# in tests/preset_level.c ROLES); a short, peaky sound whose boost stops at the peak's bound is listed, not failed
if python3 -c "import numpy, scipy" 2>/dev/null; then
    $SYN -o "$OUT/preset_level" tests/preset_level.c -lm
    run "Stallion: every factory preset within 1.5 dB of its level target (tests/preset_level.c, tools/level_presets.py --check)" \
        sh -c "'$OUT/preset_level' /dev/null '$OUT/presets.txt' '$OUT/presets.raw' '$OUT/presets.peak' &&
               python3 tools/level_presets.py '$OUT/presets.raw' '$OUT/presets.txt' 'peaks=$OUT/presets.peak' --check"
else
    echo "== skip preset levels (no numpy / scipy)"
fi
# Stallion 1.0: every factory preset centred (rule 5): a held note and a held chord, L and R within 1 dB
$SYN -o "$OUT/preset_patch_test" tests/preset_patch_test.c -lm
run "Stallion: the factory presets' patching side tables (core.h preset_patch_t): every preset loads exactly its cables and extras, its other matrix slots clear, no dead destination" "$OUT/preset_patch_test"
$SYN -o "$OUT/balance_test" tests/balance_test.c -lm
run "Stallion: every factory preset of every engine centred: a held note and a held 4-note chord, L and R within 1 dB (a UNISON sound: 16 note-ons)" "$OUT/balance_test"
$SYN -o "$OUT/fmx_kill_test" tests/fmx_kill_test.c -lm
run "FMX: a voice given up (voice budget, overload) and played again starts from rest" "$OUT/fmx_kill_test"
$SYN -o "$OUT/model_switch_test" tests/model_switch_test.c -lm
run "filter MODEL changed under a held chord (ANALOG, WAVE, VECTOR): no click" "$OUT/model_switch_test"
run "the editor protocol and the other inputs (USB / TRS MIDI, damaged flash) fuzzed under ASan + UBSan, and its reproducers" sh tests/editor_fuzz.sh "${EDFZ_N:-2000}" 1
$SYN -o "$OUT/editor_p15_test" tests/editor_p15_test.c -lm
run "Stallion: editor protocol 15: every parameter by a 14-bit id, the FM-1's settings, the values 127.. in DUMP / UP_GET / UP_PUT, protocol 14 as it was" "$OUT/editor_p15_test"
$SYN -DBOARD_SMK37=1 -o "$OUT/multi_test" tests/multi_test.c -lm
run "Stallion (SMK-37): multi sounds: MULTI_LIST / GET / PUT / LOAD / ERASE, links by name (a reordered bank, a missing user preset: nothing changed), M01..M16 on SAVE > USER, FACTORY RESET" "$OUT/multi_test"
$SYN -o "$OUT/editor_bridge" tests/editor_bridge.c -lm
mkdir -p "$OUT/factory_reset" "$OUT/factory_reset_smk"
$SYN -o "$OUT/factory_reset_ui_test" tests/factory_reset_ui_test.c -lm
run "Stallion: FACTORY RESET asks twice (one press, a timeout, any other key, HOME, a key: nothing), ERASE FAILED plays on (audio, USB back) and holds the saves, over a warm restart too, then a fresh install's state with the panel table kept, no rescue boot" "$OUT/factory_reset_ui_test" "$OUT/factory_reset"
# the same on the SMK-37 build (tests/hostsim.c takes the board's smk_input.h; the TRS MIDI OUT ring with a UART that
# is always done), and its own ways to cancel: the keys past the 27 note keys and the pads as DRUM KIT / STEPS
$SYN -DBOARD_SMK37=1 -o "$OUT/factory_reset_ui_test_smk" tests/factory_reset_ui_test.c -lm
run "Stallion: FACTORY RESET on the SMK-37 build: the same, and the outer keys (0..4, 32..36) and the pads as drums / steps cancel" "$OUT/factory_reset_ui_test_smk" "$OUT/factory_reset_smk"
if $SYN_UB -o "$OUT/fmx_ub" tests/fmx_test.c -lm 2>/dev/null; then
    run "Clipper 0.6 FMX under ASan + UBSan" sh -c "'$OUT/fmx_ub' >/dev/null && echo clean"
fi
$SYN -o "$OUT/wseq_test" tests/wseq_test.c -lm
mkdir -p build/wseq_demo
run "Clipper 0.6 WSEQ: steps at the tempo and their pitches, FWD / BACK / PING / ONCE, PITCH, START, XFADE, no click at a cut" "$OUT/wseq_test" build/wseq_demo
mkdir -p "$OUT/fmx_ui"
$SYN -o "$OUT/fmx_ui_test" tests/fmx_ui_test.c -lm
run "Clipper 0.6 FMX on the FM-1: its pages on FMX only, OP / ROUTE / templates / a sound loaded" "$OUT/fmx_ui_test" "$OUT/fmx_ui"
mkdir -p "$OUT/hist_ui"
$SYN -o "$OUT/hist_ui_test" tests/hist_ui_test.c -lm -lpthread
run "Clipper 0.7 on the FM-1: UNDO (steps, back, forward), COMPARE, EDITED, a sound loaded, RANDOM by section and FMX's operators, FAV, KBD HOLD, 15 arp modes, the arp kept" "$OUT/hist_ui_test" "$OUT/hist_ui"
$SYN -o "$OUT/split_test" tests/split_test.c -lm -lpthread
run "Clipper 0.7, the voices on both cores: every preset, the same output with and without the split (one core, inline)" "$OUT/split_test"
run "Clipper 0.7, the voices on both cores: core 1 as a thread (CORES=2)" env CORES=2 "$OUT/split_test"
run "Clipper 0.7, the voices on both cores: an erratic core 1 (CORES=2 CHAOS=5)" env CORES=2 CHAOS=5 "$OUT/split_test" 16
run "Clipper 0.7, the voices on both cores: a lost job (held, its voices here, once)" env CPU1_TIMEOUT_AT=40 "$OUT/split_test" 16
$SYN -DVS_CHECK=1 -o "$OUT/split_check" tests/split_test.c -lm -lpthread
run "PALOMINO, PONY: the bench build's check (VS_CHECK=1) finds core 1's voices as this core renders them (CORES=2 CHAOS=5)" env CORES=2 CHAOS=5 "$OUT/split_check" 32
if ${CC_TSAN:-gcc} -O1 -g -w -fsanitize=thread -DCLIPPER_SYNTH=1 -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/split_tsan" tests/split_test.c -lm -lpthread 2>/dev/null; then
    run "Clipper 0.7, the voices on both cores under ThreadSanitizer (CORES=2: no race)" env CORES=2 TSAN_OPTIONS=halt_on_error=1 "$OUT/split_tsan" 12
else
    echo "(split under ThreadSanitizer: this compiler has none, skipped)"
fi
$SYN -o "$OUT/synth8_test" tests/synth8_test.c -lm
run "Clipper 0.8: PONY (presets, tune, PW, HPF, the chorus I / II / I+II, no click, as an INSERT), VECTOR (corners, MOVE), the MOD SEQ (KEY, SYNC, LEN, SMTH, to CUT / AMP, projects, user presets), SPICE / DICE (arp, sequencer)" "$OUT/synth8_test"
if $SYN_UB -o "$OUT/synth8_ub" tests/synth8_test.c -lm 2>/dev/null; then
    run "Clipper 0.8 under ASan + UBSan" sh -c "'$OUT/synth8_ub' >/dev/null && echo clean"
fi
if $SYN_UB -o "$OUT/palomino_ub" tests/palomino_test.c -lm 2>/dev/null; then
    run "PALOMINO under ASan + UBSan" sh -c "'$OUT/palomino_ub' >/dev/null && echo clean"
fi
if $SYN_UB -o "$OUT/stampede_ub" tests/stampede_test.c -lm 2>/dev/null; then
    run "STAMPEDE under ASan + UBSan" sh -c "'$OUT/stampede_ub' nocost >/dev/null && echo clean"
fi
if $SYN_UB -o "$OUT/rodeo_ub" tests/rodeo_test.c -lm 2>/dev/null; then
    run "RODEO under ASan + UBSan" sh -c "ROD_NOCPU=1 '$OUT/rodeo_ub' >/dev/null && echo clean"
fi
if $SYN_UB -o "$OUT/mare_ub" tests/mare_test.c -lm 2>/dev/null; then
    run "MARE under ASan + UBSan" sh -c "'$OUT/mare_ub' nocost >/dev/null && echo clean"
fi
mkdir -p "$OUT/mseq_ui"
$SYN -o "$OUT/mseq_ui_test" tests/mseq_ui_test.c -lm -lpthread
run "Clipper 0.8 / 0.9 on the FM-1: MOD SEQ pages (lane, step, value, SHAPE), MOD SEQ 2, SPICE, UNDO of a step, the layer SWAP, PONY / VECTOR in the browser, TUNING, VINTG, MODL, TURNG's labels" "$OUT/mseq_ui_test" "$OUT/mseq_ui"
mkdir -p "$OUT/matrix_ui"
$SYN -o "$OUT/matrix_ui_test" tests/matrix_ui_test.c -lm -lpthread
run "Stallion MATRIX page: SOURCE / DESTINATION in the web panel's order, signal colours, the large header, MARE's normals and ARP in words, MATRIX 2 > CLEAR" "$OUT/matrix_ui_test" "$OUT/matrix_ui"
$SYN -o "$OUT/synth9_test" tests/synth9_test.c -lm
run "Clipper 0.9: the filter MODELs (SVF MOOG MS20 DIODE SEM NOTCH BP HP: bounded, responses, switch without a click), VINTAGE, the TUNING (JUST, 19-EDO, RAST, a USER scale), the TURNG arp and LOOP, projects and user presets" "$OUT/synth9_test"
if $SYN_UB -o "$OUT/synth9_ub" tests/synth9_test.c -lm 2>/dev/null; then
    run "Clipper 0.9 under ASan + UBSan" sh -c "'$OUT/synth9_ub' >/dev/null && echo clean"
fi
$SYN -o "$OUT/synth10_test" tests/synth10_test.c -lm
run "Clipper 1.0: SPREAD (left = right at 0, the middle kept), the matrix's 12 slots, LFO 3, ENV 2 LOOP, the MACROS (and CC 16..19), HALL and SHIMMER, the delay's PING and TAPE (a change of MODE: no click), projects and user presets (the tail), the pages" "$OUT/synth10_test"
if $SYN_UB -o "$OUT/synth10_ub" tests/synth10_test.c -lm 2>/dev/null; then
    run "Clipper 1.0 under ASan + UBSan" sh -c "'$OUT/synth10_ub' >/dev/null && echo clean"
fi
$SYN -o "$OUT/plate_test" tests/plate_test.c -lm
run "PLATE / HALL: the moving diffusers gentle (no pulse on a held note, as steady as ROOM within 6 %), still a reverb" "$OUT/plate_test"
$SYN -o "$OUT/shimmer_test" tests/shimmer_test.c -lm
run "SHIMMER: no flutter (a diffuse tail's power steady at the taps' crossing rate), an octave up, bounded" "$OUT/shimmer_test"
$SYN -o "$OUT/plate_taps_test" tests/plate_taps_test.c -lm
run "PLATE / HALL output taps: every tap inside its line (no chop at a line's wrap rate), both sides alike and wide" "$OUT/plate_taps_test"
$SYN -o "$OUT/plate_idle_test" tests/plate_idle_test.c -lm
run "PLATE / HALL at rest: nothing sent, the tank skipped (silent); a tail rings out in full first; back at once" "$OUT/plate_idle_test"
$SYN -o "$OUT/synth11_test" tests/synth11_test.c -lm
run "after Clipper 1.0: P>VCO (only PALOMINO hears it), the matrix's ERATE (KEY: the envelopes' key follow), KEY > CUT, projects and user presets (1.0's places kept, its saves read), the pages" "$OUT/synth11_test"
if $SYN_UB -o "$OUT/synth11_ub" tests/synth11_test.c -lm 2>/dev/null; then
    run "after Clipper 1.0 under ASan + UBSan" sh -c "'$OUT/synth11_ub' >/dev/null && echo clean"
fi
# Stallion: GLO > TOUCH (specs/touch): KEY TOUCH on simulated converter readings, the AT shaping's numbers, and the
# settings, the calibration and every pressure source end to end (the UI, the audio, the settings flash)
$CCS -O1 -w -o "$OUT/key_touch_test" tests/key_touch_test.c -lm
run "Stallion KEY TOUCH: the state machine, velocity monotonic (R4), learning (R5), pressure (R6, R7), the 12 ms gate (R3), TOUCH CAL's medians (R9)" "$OUT/key_touch_test"
$CCS -O1 -w -o "$OUT/at_shape_test" tests/at_shape_test.c -lm
run "Stallion AT shaping: CURVE within 1 step, OFFSET, ATTACK / RELEASE within 10 %, the defaults the identity (R11-R13, R15)" "$OUT/at_shape_test"
$CCS -O1 -w -o "$OUT/touch_ctl_test" tests/touch_ctl_test.c -lm
run "Stallion LEVER and RIBBON: the S-curve and its 4/3 return, both buttons, the ribbon's steps, the spring, HOLD, the 20 ms ramp, MIDI out (R17, R18, R21-R23, R25, R34)" "$OUT/touch_ctl_test"
$SYN -o "$OUT/touch_test" tests/touch_test.c -lm
run "Stallion GLO > TOUCH end to end: OFF at 100, VEL, VEL+AT on its own note's voices, AT OUT, MIDI pressure shaped, TOUCH CAL, the settings flash, the lever and the ribbon on the real UI, PRESS BEND (R1-R3, R8-R10, R13-R23, R27-R29, R32-R34)" "$OUT/touch_test"
if $SYN_UB -o "$OUT/touch_ub" tests/touch_test.c -lm 2>/dev/null && $CCS -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=all -o "$OUT/key_touch_ub" tests/key_touch_test.c -lm 2>/dev/null &&
   $CCS -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=all -o "$OUT/touch_ctl_ub" tests/touch_ctl_test.c -lm 2>/dev/null; then
    run "Stallion GLO > TOUCH under ASan + UBSan" sh -c "'$OUT/touch_ub' >/dev/null && '$OUT/key_touch_ub' >/dev/null && '$OUT/touch_ctl_ub' >/dev/null && echo clean"
fi
# Stallion: the MOTION lanes (specs/touch R35-R38): recorded over live recording, played back, overdub, the clear
# gesture, MOTION REC OFF, the stores (inside the project), the pattern tools, MIDI out
$SYN -o "$OUT/motion_test" tests/motion_test.c -lm
run "Stallion MOTION lanes: BEND / RIBBON / PRESS recorded and played back smooth, overdub where the hand moved, EDIT + KNOB 4 clears, MOTION REC OFF, save / load, old projects as before, sections and LENGTH x2, MIDI out (R35-R38)" "$OUT/motion_test"
if $SYN_UB -o "$OUT/motion_ub" tests/motion_test.c -lm 2>/dev/null; then
    run "Stallion MOTION lanes under ASan + UBSan" sh -c "'$OUT/motion_ub' >/dev/null && echo clean"
fi
# Stallion: NOTE PRESS (specs/touch R39): each recorded note's own poly pressure, played back on its own voices
$SYN -o "$OUT/note_press_test" tests/note_press_test.c -lm
run "Stallion NOTE PRESS: a chord recorded with pressure on one note plays it back on that note's voice alone (PRESS BEND +2 st, the others within 1 ct), smooth, the hand over it, undo / clear / SHIFT / LENGTH x2, save / load, old projects bit for bit (R39, R37)" "$OUT/note_press_test"
if $SYN_UB -o "$OUT/note_press_ub" tests/note_press_test.c -lm 2>/dev/null; then
    run "Stallion NOTE PRESS under ASan + UBSan" sh -c "'$OUT/note_press_ub' >/dev/null && echo clean"
fi
$SYN -o "$OUT/reverb_shimmer_test" tests/reverb_shimmer_test.c -lm
run "REVERB 2 > SHIM: at 0 every type bit for bit as before, SHIMR = HALL + 127; on ROOM / PLATE / HALL an octave up, bounded, no flutter, the tank still rests; projects" "$OUT/reverb_shimmer_test"
if $SYN_UB -o "$OUT/reverb_shimmer_ub" tests/reverb_shimmer_test.c -lm 2>/dev/null; then
    run "REVERB 2 > SHIM under ASan + UBSan" sh -c "'$OUT/reverb_shimmer_ub' >/dev/null && echo clean"
fi
$SYN -o "$OUT/lfo_chaos_test" tests/lfo_chaos_test.c -lm
run "Clipper: CHAO, the LFOs' chaotic wave (bounded, no period, smooth, at RATE, its own per note in KEY, the older waves bit for bit, kept)" "$OUT/lfo_chaos_test"
if $SYN_UB -o "$OUT/lfo_chaos_ub" tests/lfo_chaos_test.c -lm 2>/dev/null; then
    run "Clipper CHAO under ASan + UBSan" sh -c "'$OUT/lfo_chaos_ub' >/dev/null && echo clean"
fi
$SYN -o "$OUT/matrix_dest_test" tests/matrix_dest_test.c -lm
run "Matrix patching: the per-part destinations ARP (part 1's arp), DLY / REV / CHO (the sends), LFO1 (LFO 1's rate), E1AMT (the ENV DEST depths)" "$OUT/matrix_dest_test"
if $SYN_UB -o "$OUT/matrix_dest_ub" tests/matrix_dest_test.c -lm 2>/dev/null; then
    run "Matrix per-part destinations under ASan + UBSan" sh -c "'$OUT/matrix_dest_ub' >/dev/null && echo clean"
fi
$SYN -o "$OUT/engine_order_test" tests/engine_order_test.c -lm
run "engine IDs permanent (as at 2c48fa6, LOFI's 3 retired), the display order ENG_ORDER: every live engine once" "$OUT/engine_order_test"
$SYN -o "$OUT/engine_dests_test" tests/engine_dests_test.c -lm
run "Matrix destinations per engine: mx_dst_live from the engines' facts, no factory preset into a dead one, a sound brought to another engine (MORPH / UNDO, user preset, project, the main loop) loses only its dead cables and sounds bit for bit the same, INFO's masks, the editor's writes of a dead one refused" "$OUT/engine_dests_test"
$SYN -o "$OUT/clean_test" tests/clean_test.c -lm
run "Clipper 1.0 audio: no click or crackle (62 cases: presets of every engine, steals, mute, level, pan, INSERT, DIST, the sends, FILT, MODL, CHOR, VINTAGE, delay TIME and MODE, reverb SIZE and TYPE, EQ, WAVE TABL, FMX PTCH, engine switch, TUNING, LAYER, MOD SEQ, SPREAD, the MACROS, SLOPE, FILTER 2)" "$OUT/clean_test"
if $SYN_UB -o "$OUT/clean_ub" tests/clean_test.c -lm 2>/dev/null; then
    run "Clipper 0.9 audio under ASan + UBSan" sh -c "'$OUT/clean_ub' >/dev/null && echo clean"
fi
$SYN -DNVOICE=16 -o "$OUT/unison_test" tests/unison_test.c -lm
run "UNISON / UNI2 / UNI4 (16 voices): every oscillator from a random phase (no bang at the onset, the noise not stacked), UNISON on 8 voices, as wide as UNI4, as loud as POLY" "$OUT/unison_test"
$SYN -o "$OUT/unison8_test" tests/unison_test.c -lm
run "UNISON / UNI2 / UNI4 (8 voices)" "$OUT/unison8_test"
run "load-steal: the voice cost table is the fm1-emu bench's (tools/gen_vcost.py --check)" python3 tools/gen_vcost.py --check
run "the factory bank files presets-bank-*.txt are the firmware's presets and BANK[] kinds (tools/gen_bank_files.py --check)" python3 tools/gen_bank_files.py --check
$SYN -DNVOICE=16 -o "$OUT/load_steal_test" tests/load_steal_test.c -lm
run "load-steal (16 voices): voices given up at the note-on before the CPU overloads (a sudden chord on MARE), all 16 on a light engine, tails first, UNI4 stacks whole, faded, MONO / LEGATO kept, the run-time correction converges (1.6 x; from power-on at 1, 2, 2.5 x)" "$OUT/load_steal_test"
$SYN -o "$OUT/load_steal8_test" tests/load_steal_test.c -lm
run "load-steal (8 voices)" "$OUT/load_steal8_test"
$SYN -o "$OUT/pressure_test" tests/pressure_test.c -lm
run "voice-pressure (8 voices): a played PONY PAD take from power-on on a chip 1, 2, 2.5 x slower than fm1-emu (FM-1 and SMK-37 boot corrections): no late half, no shed, no held note lost, loud voices given up fade >= 5 ms, the correction learns the chip" "$OUT/pressure_test"
$SYN -DNVOICE=16 -o "$OUT/pressure16_test" tests/pressure_test.c -lm
run "voice-pressure (16 voices)" "$OUT/pressure16_test"
$SYN -o "$OUT/fx_sweep_test" tests/fx_sweep_test.c -lm
run "Clipper 1.0 effects: a sine through every reverb type, delay MODE and TIME, chorus, swept INSERT, PONY chorus and the master at their extremes: no click, left as clean as right; the chorus's RATE jumped at 16 phases of its LFO" "$OUT/fx_sweep_test"
if $SYN_UB -o "$OUT/fx_sweep_ub" tests/fx_sweep_test.c -lm 2>/dev/null; then
    run "Clipper 1.0 effects under ASan + UBSan (no line read out of its array)" sh -c "'$OUT/fx_sweep_ub' >/dev/null && echo clean"
fi
$SYN -o "$OUT/insert_mfx_test" tests/insert_mfx_test.c -lm
run "Clipper 1.1 INSERT after the PALOMINO-08: PITCH / PIT2 on pitch, PHAS6's three notches, DELAY's echoes and its tail, LOCMP's squash and lo-fi, LOCMP in user presets" "$OUT/insert_mfx_test"
if $SYN_UB -o "$OUT/insert_mfx_ub" tests/insert_mfx_test.c -lm 2>/dev/null; then
    run "Clipper 1.1 INSERT types under ASan + UBSan" sh -c "'$OUT/insert_mfx_ub' >/dev/null && echo clean"
fi
$SYN -o "$OUT/stereo_delay_test" tests/stereo_delay_test.c -lm
run "the INSERT's stereo analog delay STDLY / PPDLY: echo times, darker as TIME rises, the riff building as the original's, bounded at FDBK 127 and a tail dying to 0, left and right within 1 dB on a centred input, PPDLY crossing, no click when TIME or SPACE turns" "$OUT/stereo_delay_test"
if $SYN_UB -o "$OUT/stereo_delay_ub" tests/stereo_delay_test.c -lm 2>/dev/null; then
    run "STDLY / PPDLY under ASan + UBSan" sh -c "'$OUT/stereo_delay_ub' >/dev/null && echo clean"
fi
$SYN -o "$OUT/osc_far_test" tests/osc_far_test.c -lm
run "Clipper 1.1 ANALOG's band-limiting out of its sample loop: the oscillators bit for bit as blep's" "$OUT/osc_far_test"
$SYN -o "$OUT/engines3_test" tests/engines3_test.c -lm
run "Clipper 0.5 VPS (vector phaseshaping: pitch, formant, D, no DC, no aliasing, no step) and TERRAIN (every surface pitched, RAD, DRIFT, no aliasing), bounded, cost" "$OUT/engines3_test" build/eng3_demo
if $SYN_UB -o "$OUT/engines3_ub" tests/engines3_test.c -lm 2>/dev/null; then
    run "Clipper 0.5 engines under ASan + UBSan" sh -c "'$OUT/engines3_ub' >/dev/null && echo clean"
fi
$SYN -o "$OUT/sloop26_fixes" tests/sloop26_fixes_test.c -lm
run "Clipper 0.4.1: SLOOP 2.6's fixes (look-ahead limiter, filter start, FM6 patch after the fade, arp #191, wheels)" "$OUT/sloop26_fixes"
if $SYN_UB -o "$OUT/sloop26_ub" tests/sloop26_fixes_test.c -lm 2>/dev/null; then
    run "Clipper 0.4.1 fixes under ASan + UBSan" sh -c "'$OUT/sloop26_ub' >/dev/null && echo clean"
fi
if $SYN_UB -o "$OUT/engines2_asan" tests/engines2_test.c -lm 2>/dev/null; then
    run "Clipper 0.2 new engines under ASan + UBSan" sh -c "'$OUT/engines2_asan' >/dev/null && echo clean"
fi
if $SYN_UB -o "$OUT/clipper_ui_asan" tests/clipper_ui_test.c -lm 2>/dev/null; then
    mkdir -p "$OUT/clipper_asan"
    run "Clipper synth: UI under ASan + UBSan" sh -c "'$OUT/clipper_ui_asan' '$OUT/clipper_asan' >/dev/null && echo clean"
fi
if $SYN_UB -o "$OUT/pony_asan" tests/pony_test.c -lm 2>/dev/null; then
    run "PONY's panel under ASan + UBSan" sh -c "'$OUT/pony_asan' >/dev/null && echo clean"
fi
if $SYN_UB -o "$OUT/param_names_asan" tests/param_names_test.c -lm 2>/dev/null; then
    run "every engine's EDIT values as shown, under ASan (the FMX ALG / PTCH names ended with a 0: FM KICK ALG past 32 crashed)" "$OUT/param_names_asan"
fi
# Stallion R6, R9: no trademark word (tests/denylist.txt) in a name, label, value, string, web page or doc (but history and tests/denylist_docs_allow.txt)
$SYN -o "$OUT/denylist_dump" tests/denylist_dump.c -lm
run "trademark deny-list: engine, preset, kit names, labels, values, firmware strings, web pages, docs (Stallion R6)" \
    sh -c "'$OUT/denylist_dump' > '$OUT/denylist_names.txt' && python3 tests/denylist_test.py '$OUT/denylist_names.txt'"

$CC -o "$OUT/upreset_test" tests/upreset_test.c
run "user presets (UP_PUT parser, bank round trip, versions)" "$OUT/upreset_test"
$SYN -o "$OUT/upreset_syn_test" tests/upreset_test.c -lm
run "user presets, preset format 2: 24 matrix slots and every value through a power cycle, Clipper / SLOOP banks read as empty and left in flash" "$OUT/upreset_syn_test"

$CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"
$CC -Ifirmware/hal -o "$OUT/encoder_test" tests/encoder_test.c
run "knobs: one click = one step (slow, fast, pauses, bounce)" "$OUT/encoder_test"
HALF=$(sed -n 's/^#define HALF_FRAMES \([0-9]*\).*/\1/p' firmware/src/core.h)
$CC -DT_CDC=1 -DHALF_FRAMES=$HALF -o "$OUT/uac_test" tests/uac_test.c -lm
run "USB audio input: descriptors (with CDC), ring and packets" "$OUT/uac_test"
$CC -DT_CDC=0 -DHALF_FRAMES=$HALF -o "$OUT/uac_test_nocdc" tests/uac_test.c -lm
run "USB audio input: descriptors (without CDC), ring and packets" "$OUT/uac_test_nocdc"
$CC -DT_CDC=2 -DHALF_FRAMES=$HALF -o "$OUT/uac_test_seroff" tests/uac_test.c -lm
run "USB audio input: descriptors (CDC built in, menu USB SERIAL OFF), ring and packets" "$OUT/uac_test_seroff"
run "USB SERIAL OFF: the descriptors of a build without CDC, byte for byte" \
    sh -c "[ \"\$(UAC_DUMP=1 '$OUT/uac_test_seroff' | tail -n 2)\" = \"\$(UAC_DUMP=1 '$OUT/uac_test_nocdc' | tail -n 2)\" ] && echo same"
uac_in_app() { ${CC%% *} -E -Ibuild/gen -Ifirmware/hal -Ifirmware/src firmware/src/felucca.c 2>/dev/null | grep -q uac_service; }
run "USB audio input: built into the firmware (FELUCCA_UAC set before usb.c)" uac_in_app

if [ -z "$HOST_ONLY" ]; then
$CC -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry" "$OUT/ota_test" build/felucca.fwsc

head -c 200000 build/felucca.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc
else
    echo "== skip update entry and loader (HOST_ONLY: no build/felucca.fwsc)"
fi
# the update loader's screen (firmware/loader/ldr_screen.c): the boot logo tools/gen_logo.py makes (needs Pillow) and
# the bar, on a fake panel (tests/ldr_screen_hal: deadlines, a dead panel)
if python3 -c "import PIL" 2>/dev/null; then
    run "loader logo: build/gen/ldr_logo.h made (the boot splash, 240 x 184)" \
        sh -c "python3 tools/gen_logo.py build/gen/ldr_logo.h && grep -q 'define SLOOP_SPLASH_W 240' build/gen/ldr_logo.h"
    $CC -Itests/ldr_screen_hal -Ibuild/gen -o "$OUT/ldr_screen_test" tests/ldr_screen_test.c
    run "update loader screen: the boot logo and a thick bar that only fills, deadlines, a dead panel turns the screen off" "$OUT/ldr_screen_test"
    $CC -DBOARD_SMK37=1 -Itests/ldr_screen_hal -Ibuild/gen -o "$OUT/ldr_screen_test_smk" tests/ldr_screen_test.c
    run "update loader screen, SMK-37 Pro: the app's ST7789V start-up table, the same deadlines, the pads left alone" \
        "$OUT/ldr_screen_test_smk"
else
    echo "== skip update loader screen (no Pillow for tools/gen_logo.py)"
fi
$CC -DBOARD_SMK37=1 -Ifirmware/hal -o "$OUT/ldr_lcd_pins_test" tests/ldr_lcd_pins_test.c
run "update loader screen, SMK-37 Pro pins: the LCD on SPI1 group A, the pad LEDs' line PC10 untouched" \
    "$OUT/ldr_lcd_pins_test"

# SMK-37 Pro (BOARD_SMK37): the flash map, the OTA staging and the loader windows on SMK-layout packages
# (made here from the FM-1 binaries: only the layout and the protocol are under test)
$CC -DBOARD_SMK37=1 -Ifirmware/hal -o "$OUT/smk_flash_test" tests/smk_flash_test.c
run "SMK-37 Pro flash map: allow-lists never reach the head, BTIF/USRTRIM, USR, key_mac" "$OUT/smk_flash_test"
if [ -z "$HOST_ONLY" ]; then
python3 tools/fm1pkg_make.py --board smk37 build/felucca.bin build/loader/ota.bin "$OUT/smk_new.fwsc" >/dev/null
python3 tools/fm1pkg_make.py --board smk37 "$OUT/old_app.bin" build/loader/ota.bin "$OUT/smk_old.fwsc" >/dev/null
run "SMK-37 Pro package layout (fm1pkg_make --board smk37): PRCT 0x9D000, stock region table"     python3 tests/smk_pkg_test.py "$OUT/smk_new.fwsc"
$CC -DBOARD_SMK37=1 -o "$OUT/ota_test_smk" tests/ota_test.c
run "M-UPGRADE entry, SMK-37 Pro build (staging 0xBB000, parm ota-SMK-37 Pro_016)" "$OUT/ota_test_smk" "$OUT/smk_new.fwsc"
for m in 1 2; do                         # the MODEL=elite / mke builds of ota.c
    $CC -DBOARD_SMK37=1 -DT_MODEL=$m -o "$OUT/ota_test_smk_m$m" tests/ota_test.c
    run "M-UPGRADE entry, SMK-37 model $m (1 Elite, 2 MKE-P37): identity and parm under its name" "$OUT/ota_test_smk_m$m" "$OUT/smk_new.fwsc"
done
for m in "elite:SMK-37 Elite_104200" "mke:MKE-P37_104200"; do   # (Stallion's own number, a dev build)
    f="build/stallion-smk37-${m%%:*}.fwsc"
    [ -f "$f" ] && run "SMK-37 ${m%%:*} build package: layout, identity ${m#*:}" python3 tests/smk_pkg_test.py "$f" "${m#*:}"
done
$CC -DBOARD_SMK37=1 -o "$OUT/ldr_test_smk" tests/ldr_test.c
run "update loader, SMK-37 Pro: app area 0x4000-0x9CFFF, protected ranges untouched"     "$OUT/ldr_test_smk" "$OUT/smk_old.fwsc" "$OUT/smk_new.fwsc"
else
    echo "== skip the SMK-37 package, update entry and loader tests (HOST_ONLY: no build/felucca.bin)"
fi

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/hostsim" tests/hostsim.c -lm
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/scale_test" tests/scale_test.c -lm
run "scales: white-key mapping and note lifecycle" "$OUT/scale_test"
run "DSP render (ANALOG preset 0)" "$OUT/hostsim" 0 0 1 "$OUT/render.wav"
mkdir -p build/tracks_demo
run "TRACKS: 4-track pattern, live recording (lengths, swing), voice budget, engine switch, cost" env TRACKS=build/tracks_demo "$OUT/hostsim" 0 0 1 "$OUT/tracks.wav"
$CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/project_test" tests/project_test.c -lm
run "project formats (FUN4 / FUN3 / FUN2 / FUN1 -> FUN5), capture / apply, autosave" "$OUT/project_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/slicer_test" tests/slicer_test.c -lm
mkdir -p build/slicer_demo
run "SLICER: no clicks, timing, sync with the sequencer, STUT, cost, demos" "$OUT/slicer_test" build/slicer_demo
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/fm6_test" tests/fm6_test.c -lm
mkdir -p build/fm6_demo
run "FM6: algorithms, envelopes, retrigger, DC, clipping, macros, patch formats, voices, the bank, demos" "$OUT/fm6_test" build/fm6_demo
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/hybrid_test" tests/hybrid_test.c -lm
mkdir -p build/hybrid_demo
run "HYBRID (Clipper): oscillators, filters, matrix, controllers, patches, cost, demos" "$OUT/hybrid_test" build/hybrid_demo
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/hybrid_editor_test" tests/hybrid_editor_test.c -lm
run "HYBRID editor commands (v11): HY_GET, HY_PUT, ranges, bad requests" "$OUT/hybrid_editor_test"
# (Stallion) the synth's factory patches (its names: SYN FLUTE is SOFT LEAD there) for web/test_web.mjs, which checks the
# editor's mock against them: the editor talks to the synth
$SYN -o "$OUT/hybrid_editor_syn" tests/hybrid_editor_test.c -lm
run "HYBRID editor commands (v11), the synth build: its factory patches for the editor's mock" "$OUT/hybrid_editor_syn" "$OUT/hy_factory.json"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/regress" tests/regress.c -lm
# the CPU budget: counted by the kernel on macOS; elsewhere under callgrind when valgrind is there (exact, ~45 s;
# SKIP_CPU_VALGRIND=1 to time instead, which is only a rough check)
if [ -z "$SKIP_CPU_VALGRIND" ] && command -v valgrind >/dev/null 2>&1; then export CPU_VALGRIND=1; fi
run "regression: golden renders, health, voices, CPU budget" "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt
# SLICE (tests/slice_test.c) needs a FELUCCA_SLICE=1 build; the engine is not built by default

if [ -z "$HOST_ONLY" ]; then
run "regression: target cost of the render loops" python3 tests/target_budget.py \
    build/felucca.dis tests/target_budget.txt
fi

run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py
# the tune hooks (#ifndef NAME / #define NAME literal / #endif in an engine): never set by a device build,
# engine-local, the same preprocessed code as the plain #define (the device image unchanged)
run "tune hooks: host-only engine constants (to try values): no device -D, engine-local, same code as the plain define" \
    python3 tests/tune_hooks_test.py
# Stallion R52, R55: SPDX headers and assets/MANIFEST; R12: the private-files check and its bypasses (throwaway repos)
run "licence audit: every source file's SPDX header, every data file in assets/MANIFEST" python3 tools/licence_check.py
run "private files (R12): audio, private rig / reference / score files, riff scores stopped (staged, every commit, the tree)" \
    python3 tests/private_check_test.py
run "Stallion identity: no Clipper a user sees but in credits, the package name, the lineage, trademarks (R1-R3, R9)" python3 tests/brand_test.py

if command -v node >/dev/null 2>&1; then
    run "web pages: editor protocol, samples, packages, update protocol" node web/test_web.mjs
    run "Stallion: the editor's own code on the real firmware (protocol 15), every id and FM-1 page has a place in the editor, the offline editor the same" node web/test_editor_p15.mjs "$OUT/editor_bridge"
    run "Clipper Pro installer page: one package per model, resume, refusals, fr / en / ja (make_site --pro)" node web/test_pro.mjs
    run "Stallion SMK-37 installer page: one package per model, resume, refusals, the FM-1 / SMK-37 chooser, sw.js (make_site --smk37)" node web/test_pro.mjs --stallion
else
    echo "== skip web tests (no node)"
fi

[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }
