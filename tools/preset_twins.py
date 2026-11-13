# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
"""(Stallion 1.0) Which factory presets of an engine sound alike: each preset on a held chord (mare_render's `chord`)
and on one key held low (C2) and mid (C4), 4 s each (the same notes for every preset: register does not part them);
on each, its spectrum (Welch, 19 log bands 60 Hz .. 8 kHz, each band's mean power, dB, the bands' mean taken off) and its level envelope (50 ms frames, dB under
its own loudest, floored at -50); on each test the distance of two presets is the mean |difference| of the spectra plus
half that of the envelopes (dB); a pair's distance is its least over the tests. Lists the closest pairs; --gate D fails (exit 1) when a pair is under D dB.
  clipper-fm1/.venv/bin/python tools/preset_twins.py [--engine 26] [--render build/host/mare_render] [--dir DIR]
                                                     [--top 12] [--gate 3] [--only "A,B"]
--dir keeps the renders (one WAV a preset, by index; --reuse: those already there are used again). The names from
--src (the engine table --table, default MARE's).
--only: report only pairs touching these names (the gate too). Build mare_render first (tools/mare_render.c)."""
import argparse
import os
import re
import subprocess
import sys
import tempfile

import numpy as np
from scipy import signal
from scipy.io import wavfile

EDGES = np.geomspace(60, 8000, 20)


def feats(path):
    sr, d = wavfile.read(path)
    x = d.astype(np.float64).mean(1) if d.ndim == 2 else d.astype(np.float64)
    f, P = signal.welch(x, sr, nperseg=4096)
    sp = np.array([10 * np.log10(P[(f >= a) & (f < b)].mean() + 1e-20) for a, b in zip(EDGES[:-1], EDGES[1:])])
    sp -= sp.mean()
    h = int(0.05 * sr)
    e = np.array([np.sqrt(np.mean(x[i:i + h] ** 2)) for i in range(0, len(x) - h + 1, h)]) + 1e-9
    e = np.maximum(20 * np.log10(e / e.max()), -50)
    return sp, e


def names(src, table):
    """the presets' names in order, from the engine's source (its `static const preset_t TABLE[] = {` rows)"""
    s = open(src, encoding="utf-8").read()
    s = s[s.index(f"preset_t {table}[]"):]
    s = s[:s.index("\n};")]
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return re.findall(r'\{"([^"]+)",', s)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", type=int, default=26)
    ap.add_argument("--render", default="build/host/mare_render")
    ap.add_argument("--dir")
    ap.add_argument("--reuse", action="store_true")
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--gate", type=float)
    ap.add_argument("--tests", default="chord,held:36,held:60",
                    help="the phrases each pair is compared on (mare_render's, 4 s each); a pair's distance is its least")
    ap.add_argument("--only", default="")
    ap.add_argument("--src", default="firmware/src/eng_mare.c", help="the engine's source (the presets' names)")
    ap.add_argument("--table", default="MARE_PRESETS")
    a = ap.parse_args()
    d = a.dir or tempfile.mkdtemp()
    os.makedirs(d, exist_ok=True)
    tests = [t.strip() for t in a.tests.split(",") if t.strip()]
    F = {}
    for i, n in enumerate(names(a.src, a.table)):
        F[n] = []
        for t in tests:
            w = os.path.join(d, f"{i:02d}-{t.replace(':', '')}.wav")
            if not (a.reuse and os.path.exists(w)):
                subprocess.run([a.render, str(a.engine), str(i), t, w, "4"], capture_output=True, check=True)
            F[n].append(feats(w))
    only = {s.strip() for s in a.only.split(",") if s.strip()}
    rows = []
    ks = list(F)
    for x in range(len(ks)):
        for y in range(x + 1, len(ks)):
            A, B = ks[x], ks[y]
            if only and A not in only and B not in only:
                continue
            per = []
            for fa, fb in zip(F[A], F[B]):
                ds = float(np.mean(np.abs(fa[0] - fb[0])))
                n = min(len(fa[1]), len(fb[1]))
                de = float(np.mean(np.abs(fa[1][:n] - fb[1][:n])))
                per.append((ds + de / 2, ds, de))
            k = min(range(len(per)), key=lambda j: per[j][0])
            rows.append((per[k][0], per[k][1], per[k][2], A, B, tests[k], [p[0] for p in per]))
    rows.sort()
    print(f"{'pair':36s} {'dist':>5s} {'spec':>5s} {'env':>5s}  {'on':8s} each test ({', '.join(tests)})")
    for t, ds, de, A, B, on, per in rows[:a.top]:
        print(f"{A + ' ~ ' + B:36s} {t:5.1f} {ds:5.1f} {de:5.1f}  {on:8s} " + " ".join(f"{x:4.1f}" for x in per))
    if a.gate is not None:
        bad = [r for r in rows if r[0] < a.gate]
        print(f"gate {a.gate} dB: {len(bad)} pair(s) under")
        sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
