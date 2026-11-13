#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measures every factory preset and writes firmware/src/preset_trim.h: a level trim per preset
(1/2 dB, the steps of LEVEL), so every sound comes out as loud as the others at the same LEVEL.
Loudness: ITU-R BS.1770 integrated (gated) over a phrase that fits the sound (tests/preset_level.c:
a bass line, held chords, comping or a melody).

  cc -O2 -Ibuild/gen -Ifirmware/src -Ifirmware/hal tests/preset_level.c -lm -o build/host/preset_level
  build/host/preset_level build/host/presets.wav build/host/presets.txt build/host/presets.raw
  tools/level_presets.py build/host/presets.raw build/host/presets.txt [ENGINE]
  (rebuild and run it again until nothing moves: the trims add to the ones in preset_trim.h)
  Stallion, the synth: build/host/preset_level ... build/host/presets.peak, then add peaks=build/host/presets.peak
  (the trims past LEVEL 127, up to TRIM_MAX, as far as each sound's peak lets them), "--check" (run_tests.sh): only
  report, exit 1 if a preset is more than TOL dB off its target; every preset's phrase: preset_level.c ROLES.
  ENGINE: only that engine's presets are measured again (a new engine: the others keep their trims, so
  their golden renders do not move; SLOOP 2.4: FM6 = 9). The Clipper synth (Clipper 0.3: every engine, measured
  through the synth's own chain): build preset_level with -DCLIPPER_SYNTH=1 and add "clipper" (ENGINE: all, or one);
  the trims go to firmware/src/preset_trim_clipper.h (a preset not there yet starts from preset_trim.h's).
Needs numpy and scipy."""
import re
import sys
from pathlib import Path

import numpy as np
from scipy.signal import lfilter

FS, CTL = 44100, 32
SEG = (FS * 9 // 2) // CTL * CTL + (FS // 4) // CTL * CTL
OUT = Path(__file__).resolve().parent.parent / "firmware/src/preset_trim.h"
TARGET = -15.0                                 # LUFS (integrated), at the default LEVEL
ROLE = {0: 0.0, 1: -1.0, 2: 0.0, 3: 0.0, 4: 0.0, 5: -1.0}   # bass, chords held (pads, organs), comping, melody, one-key
                                                            # chords, one key held (Stallion: tests/preset_level.c ROLES)
SKIP = {"GM KIT", "USER WT", "USER SMP", "USER GRAIN"}   # (the drum map on a synth track; the user's own: as they are)
# trims set by hand, kept as they are and not judged against the target: chord sounds whose
# stereo envelopes stack louder on a held chord than the single-note measure here sees
OWNER_TRIM = {(26, "SPLIT CHORDS"): 29, (26, "STACK BASS"): 6}


def lufs(x):
    b1 = [1.53512485958697, -2.69169618940638, 1.19839281085285]
    a1 = [1, -1.69065929318241, 0.73248077421585]
    b2, a2 = [1, -2, 1], [1, -1.99004745483398, 0.99007225036621]
    k = lfilter(b2, a2, lfilter(b1, a1, x)) ** 2
    w, h = int(0.4 * FS), int(0.1 * FS)
    bl = np.array([k[i:i + w].mean() for i in range(0, len(k) - w, h)])
    bl = bl[bl > 1e-7]
    if not len(bl):
        return None
    g = bl[10 * np.log10(bl) - 0.691 > -70]
    rel = 10 * np.log10(g.mean()) - 0.691 - 10
    g = g[10 * np.log10(g) - 0.691 > rel]
    if not len(g):
        return None
    return 10 * np.log10(g.mean()) - 0.691


# (Stallion) the ear below 500 Hz: BS.1770's K-weighting is nearly flat from 80 Hz to 1 kHz, the ear is not (ISO 226:2003,
# the 80-phon equal-loudness contour relative to 1 kHz falls 15 dB by 63 Hz), so a sine or kick sub measured on target
# was heard far under it. EAR_DB: half of that contour's fall beyond K-weighting's own (min(0, -(Lp80(f) - Lp80(1 kHz)) -
# K(f)) / 2; half: the full one lifted every bass by 7 dB, half brings the subs in line audibly), 0 above 500 Hz: leads,
# keys and pads move by a few tenths at most. Applied zero-phase to the segment, then lufs() unchanged.
EAR_HZ = [20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500]
EAR_DB = [-25.9, -23.9, -21.8, -19.5, -17.4, -15.1, -12.8, -10.6, -8.6, -6.5, -4.8, -3.3, -1.9, -0.8, 0.0]


def loudness(x):
    """the synth's measure (main_clipper, check): lufs() after half of EAR_DB"""
    f = np.fft.rfftfreq(len(x), 1 / FS)
    g = 10 ** (np.interp(np.log10(np.maximum(f, 1)), np.log10(EAR_HZ), EAR_DB) / 40)   # (/ 40: half the dB)
    return lufs(np.fft.irfft(np.fft.rfft(x) * g, len(x)))


CL_FIRST = 0                                   # Clipper 0.3: every engine of the synth in preset_trim_clipper.h
OUT_CL = OUT.with_name("preset_trim_clipper.h")


# (Stallion) the synth's trims run past LEVEL 127 (fx.c: LEVEL + TRIM up to 190, gen_tables.py LEVEL_Q12), up to P_ED_FX's
# 63, as far as the sound's peak lets them: a boost (a trim above 0) keeps the peak before the master's limiter (MASTER
# all the way up) under PEAK_MAX (preset_level's 4th file); a short, peaky sound that would need more stays short of
# its target (--check lists it)
# PEAK_MAX +6: the factory sounds on target with no boost already reach it (comping chords: +1.4 dBFS median, +14 the
# most); at the top of MASTER the limiter (fx.c LIM_T, -5.2 dBFS) takes at most 11 dB off a boosted sound's attacks and
# nothing clips (the output's peak stays at the limiter's); 7 dB or more below the top, none. (-1 dBFS left 116 presets
# 2 to 18 dB short of their target, half of them basses.)
TRIM_MAX, PEAK_MAX, TOL = 63, 6.0, 1.5         # 1/2 dB; dBFS; dB (--check: the most a preset may be off its target)
SKIP_ENG = {4, 8}                              # (the synth: SAMPLE, GRAIN take no trim, engines.c preset_trim)


def peaks_of(path):
    return {(int(e), int(p)): (float(pk), float(gr)) for e, p, pk, gr in
            (l.split() for l in Path(path).read_text().splitlines())} if path else {}


def check(raw, rep, peaks=None):
    """run_tests.sh (Stallion): every factory sound of the synth (not SKIP) within TOL of its target; the worst ones"""
    rows = [l.split(None, 3) for l in Path(rep).read_text().splitlines()]
    d = np.fromfile(raw, np.int32).astype(float) / 32768.0
    pk = peaks_of(peaks)
    out = []
    for i, (e, p, role, name) in enumerate(rows):
        if name in SKIP or int(e) in SKIP_ENG or (int(e), name) in OWNER_TRIM:
            continue
        L = loudness(d[i * SEG:(i + 1) * SEG])
        dev = -99.0 if L is None else L - (TARGET + ROLE[int(role)])
        out.append((abs(dev), int(e), int(p), name, dev, pk.get((int(e), int(p)), (0.0, 0.0))))
    out.sort(reverse=True)
    # under its target with its peak at PEAK_MAX: as loud as the peak lets it (a short, peaky sound), listed, not a failure
    bound = [o for o in out if o[0] > TOL and o[4] < 0 and pk and o[5][0] >= PEAK_MAX - 0.5]
    bad = [o for o in out if o[0] > TOL and o not in bound]
    for a, e, p, name, dev, (k, gr) in bad[:12]:
        print(f"  {e:2d}.{p:<2d} {name:<14s} {dev:+5.1f} dB off its target  (peak {k:+5.1f} dBFS)")
    if bound:
        print(f"  {len(bound)} under their target at the peak's bound ({PEAK_MAX} dBFS), the farthest: " +
              ", ".join(f"{o[3]} {o[4]:+.1f}" for o in bound[:6]))
    print(f"preset levels: {len(out)} presets, {len(bad)} more than {TOL} dB off their target"
          + ("" if not bad else ": tools/level_presets.py (its docstring), then run this again"))
    return 1 if bad else 0


def main_clipper(raw, rep, only, peaks=None):
    """engines from CL_FIRST (a CLIPPER_SYNTH 1 build of tests/preset_level.c): preset_trim_clipper.h, the other
    engines' trims (preset_trim.h, measured on the groovebox build) untouched"""
    rows = [l.split(None, 3) for l in Path(rep).read_text().splitlines()]
    pk = peaks_of(peaks)
    d = np.fromfile(raw, np.int32).astype(float) / 32768.0
    old = {}
    if OUT.exists():                              # (a preset new to the synth table: the groovebox's trim)
        for e, name, v in re.findall(r"/\* (\d+)\.\d+ (.*?) \*/ (-?\d+)", OUT.read_text()):
            old[(int(e), name)] = int(v)
    if OUT_CL.exists():
        for e, name, v in re.findall(r"/\* (\d+)\.\d+ (.*?) \*/ (-?\d+)", OUT_CL.read_text()):
            old[(int(e), name)] = int(v)
    neng = max(int(r[0]) for r in rows) + 1
    pmax = 32
    tab = [[0] * pmax for _ in range(neng)]
    names = [[""] * pmax for _ in range(neng)]
    for i, (e, p, role, name) in enumerate(rows):
        e, p, role = int(e), int(p), int(role)
        if e < CL_FIRST:
            continue
        names[e][p] = name
        L = loudness(d[i * SEG:(i + 1) * SEG])
        t = old.get((e, name), 0)
        if e in SKIP_ENG:
            t = 0                                    # (the firmware gives them none)
        elif L is not None and name not in SKIP and (only is None or e == only):
            dl = TARGET + ROLE[role] - L
            t1 = t + (int(round(dl * 2)) if abs(dl) >= 0.35 else 0)   # (a step only past 0.35 dB: no see-saw at 0.25)
            if (e, p) in pk and t1 > 0:                  # (a boost: as far as the peak lets it; never below 0 for it)
                t1 = min(t1, max(0, t + int(np.floor((PEAK_MAX - pk[(e, p)][0]) * 2))))
            t = t1
        if (e, name) in OWNER_TRIM:
            t = OWNER_TRIM[(e, name)]
        tab[e][p] = max(-40, min(TRIM_MAX, t))
    lines = ["/* SPDX-License-Identifier: GPL-3.0-only */",
             "/* generated by tools/level_presets.py (a CLIPPER_SYNTH build): the level trims of Clipper's own engines, 1/2 dB */",
             "#pragma once", f"#define PTC_FIRST {CL_FIRST}u", f"#define PTC_ENGINES {max(1, neng - CL_FIRST)}u", f"#define PTC_MAX {pmax}u",
             "static const int8_t PRESET_TRIM_CL[PTC_ENGINES][PTC_MAX] = {"]
    for e in range(CL_FIRST, max(neng, CL_FIRST + 1)):
        cells = [f"/* {e}.{p} {names[e][p] or '-'} */ {tab[e][p]}" for p in range(pmax) if e < neng and names[e][p]]
        lines.append("    {" + (", ".join(cells) or "0") + "},")
    lines.append("};")
    OUT_CL.write_text("\n".join(lines) + "\n")
    print(f"{sum(1 for r in rows if int(r[0]) >= CL_FIRST)} presets levelled -> {OUT_CL}")


def main(raw, rep, only=None, clipper=False, peaks=None):
    if clipper:
        return main_clipper(raw, rep, only, peaks)
    rows = [l.split(None, 3) for l in Path(rep).read_text().splitlines()]
    d = np.fromfile(raw, np.int32).astype(float) / 32768.0
    old = {}
    if OUT.exists():
        for e, name, v in re.findall(r"/\* (\d+)\.\d+ (.*?) \*/ (-?\d+)", OUT.read_text()):
            old[(int(e), name)] = int(v)              # (by name: presets may move in their table)
    neng = max(int(r[0]) for r in rows) + 1
    pmax = 32
    tab = [[0] * pmax for _ in range(neng)]
    names = [[""] * pmax for _ in range(neng)]
    for i, (e, p, role, name) in enumerate(rows):
        e, p, role = int(e), int(p), int(role)
        assert p < pmax, name
        names[e][p] = name
        L = lufs(d[i * SEG:(i + 1) * SEG])
        t = old.get((e, name), 0)
        if L is not None and name not in SKIP and (only is None or e == only):
            t += int(round((TARGET + ROLE[role] - L) * 2))
        tab[e][p] = max(-40, min(23, t))       # (LEVEL 104 + 23 = 127: the top of the table)
    lines = ["/* SPDX-License-Identifier: GPL-3.0-only */",
             "/* generated by tools/level_presets.py: the level trim of each factory preset, 1/2 dB */",
             "#pragma once", f"#define PT_ENGINES {neng}u", f"#define PT_MAX {pmax}u",
             "static const int8_t PRESET_TRIM[PT_ENGINES][PT_MAX] = {"]
    for e in range(neng):
        cells = [f"/* {e}.{p} {names[e][p] or '-'} */ {tab[e][p]}" for p in range(pmax) if names[e][p]]
        lines.append("    {" + (", ".join(cells) or "0") + "},")   # (an engine with no preset: 3, core.h ENGI_GONE)
    lines.append("};")
    OUT.write_text("\n".join(lines) + "\n")
    print(f"{len(rows)} presets levelled -> {OUT}")


if __name__ == "__main__":
    pkf = next((a[6:] for a in sys.argv[3:] if a.startswith("peaks=")), None)
    if "--check" in sys.argv[3:]:
        sys.exit(check(sys.argv[1], sys.argv[2], pkf))
    args = [a for a in sys.argv[3:] if a not in ("clipper", "--check") and not a.startswith("peaks=")]
    main(sys.argv[1], sys.argv[2], int(args[0]) if args else None, "clipper" in sys.argv[3:], pkf)
