# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
"""Summarise emu/*.raw (bench_st dumps) -> TSV rows for tools/vcost_bench.tsv, or a before/after table.
usage: summ.py tsv TAGPREFIX...   |   summ.py table TAG..."""
import sys, re, pathlib
import os
S = pathlib.Path(os.environ["VCOST_DIR"])        # the bench's work folder (emu/)
HERE = pathlib.Path(__file__).resolve().parent
HALF_US = 256 * 1000000 / 44100
SPLIT = {"ANALOG", "DIGITAL", "PHASE", "TRIO", "HYBRID", "WAVE", "ACID", "STRINGS", "MACRO", "VPS", "TERRAIN",
         "FMX", "WSEQ", "PONY", "VECTOR", "PALOMINO", "STAMPEDE", "RODEO", "MARE", "FM6"}   # vs_ok (Stallion names)
NAMES = {}
for ln in (HERE / "presets.txt").read_text().splitlines():
    if ln.startswith("#"):
        continue
    e, p, n = ln.split()
    NAMES[n] = (int(e), 0 if n == 'WAVE' else int(p))


def dump(tag):
    txt = (S / "emu" / f"{tag}.raw").read_text()
    m = re.search(r"dump 0x[0-9a-f]+ \(212 words\):\n((?:\s+0x[0-9a-f]+:.*\n?)+)", txt)
    if not m:
        return None
    w = [int(x, 16) for ln in m.group(1).splitlines() for x in ln.split(":")[1].split()]
    st = dict(magic=w[0], vmax=w[1], vnow=w[2], kills=w[3], shed=w[4], resets=w[5], allowed=w[8], ratio=w[9], tn=w[10])
    tr = [(x & 0xFFFF, (x >> 16) & 0xFF, (x >> 24) & 1) for x in w[12:12 + min(st["tn"], 200)]]
    st['lim'] = [x >> 25 for x in w[12:12 + min(st["tn"], 200)]]
    return st, tr


def steady(tr):
    pre = [u for u, v, l in tr[2:16] if v == 0]
    post = [u for u, v, l in tr[60:190]]
    vs = [v for u, v, l in tr[60:190]]
    return (sum(pre) / len(pre) if pre else 0), (sum(post) / len(post) if post else 0), (max(set(vs), key=vs.count) if vs else 0)


def tsv(prefixes):
    for f in sorted((S / "emu").glob("*.raw")):
        tag = f.stem
        if not any(tag.startswith(p) for p in prefixes):
            continue
        r = dump(tag)
        if not r:
            print("#bad", tag, file=sys.stderr)
            continue
        st, tr = r
        _, name, n = tag.split("-")
        fl = 32 if tag.startswith("C-") else 32 + int(tag.split("-")[0][1:])
        idle, load, v = steady(tr)
        e, pr = NAMES[name]
        print(f"{tag}\t{e}\t{name}\t{pr}\t{fl}\t{v}\t{int(name in SPLIT)}\t{idle:.0f}\t{load:.0f}")


def table(tags):
    print(f"{'run':24} {'peak%':>6} {'late':>5} {'kept':>5} {'gave up':>8} {'shed':>5} {'settle ms':>9} {'steady%':>8}")
    for tag in tags:
        r = dump(tag)
        if not r:
            print(tag, "no dump")
            continue
        st, tr = r
        chord = next((i for i, (u, v, l) in enumerate(tr) if v > 0), 0)
        peak = max(u for u, v, l in tr[chord:]) if tr else 0
        late = sum(l for u, v, l in tr)
        # settle: from the chord's first half until 8 halves in a row under 90 % and not late
        settle, run = None, 0
        for i in range(chord, len(tr)):
            u, v, l = tr[i]
            run = run + 1 if (u < 0.90 * HALF_US and not l) else 0
            if run == 8:
                settle = (i - 7 - chord) * HALF_US / 1000
                break
        _, load, v = steady(tr)
        print(f"{tag:24} {100 * peak / HALF_US:6.1f} {late:5} {v:5} {st['kills']:8} {st['shed']:5} "
              f"{('%.0f' % settle) if settle is not None else 'never':>9} {100 * load / HALF_US:8.1f}")


if sys.argv[1] == "tsv":
    tsv(sys.argv[2:])
else:
    table(sys.argv[2:])
