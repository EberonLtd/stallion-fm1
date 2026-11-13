#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
"""The voice cost table of the CPU-pressure admission (firmware/src/voice.c vc_admit) from the fm1-emu bench.

  python3 tools/gen_vcost.py [tools/vcost_bench.tsv] [firmware/src/vcost_tables.h]    write the table
  python3 tools/gen_vcost.py --check                                                 the table is the bench's

tools/vcost_bench.tsv: one emulator run a row (the 16-voice bench build: a chord of N notes at once, POLY, shedding
off; the render's time per half read from the trace, steady after the chord):
  tag  engine  name  preset  flags  voices  split  idle_us  load_us
flags (the bench's): 32 POLY alone ("bare": no FILTER 2, no matrix slot, no SPREAD / VINTAGE, no sends / INSERT /
DIST); + 1 FILTER 2 SER, + 2 FILTER 2 SPLIT, + 4 twelve matrix slots, + 8 SPREAD and VINTAGE, + 16 the part's effects.
split: the engine's voices go over both cores (voice.c vs_ok), core 0 then waits for the bigger half.

The model (voice.c vc_pl): core 0's time for a part's n voices = (split and n >= 2 ? ceil(n / 2) : n) x a voice's
cost on one core + VC_SPLIT (the hand-over, split only) + VC_PART (the part's own work while it sounds) + VC_PFX (its DIST, INSERT, sends on); a voice's
cost = VC_ENG[engine] + VC_F2 (FILTER 2 on) + VC_SIDE (SPREAD or FILTER 2 SPLIT: the stereo side) + slots x
VC_SLOT16 / 16. All in 1/4096 of the half buffer (256 samples, 5804 us). The fit: per engine a line through its bare
runs (time against ceil(n / 2) or n); the features from their runs on four engines against the bare ones."""
import sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
GONE = {3: "-"}   # (Stallion 1.0) an engine number kept with no engine behind it (3: was LOFI): cost 0, nothing plays it
HALF_US = 256 * 1000000 // 44100
Q = 4096


def q(us):
    return us * Q / HALF_US


def x_of(n, split):
    return (n + 1) // 2 if split and n >= 2 else n


def line(pts):
    """least squares y = a + b x"""
    n = len(pts)
    sx = sum(p[0] for p in pts); sy = sum(p[1] for p in pts)
    sxx = sum(p[0] * p[0] for p in pts); sxy = sum(p[0] * p[1] for p in pts)
    d = n * sxx - sx * sx
    if n < 2 or d == 0:
        return (sy / n if n else 0.0), 0.0
    b = (n * sxy - sx * sy) / d
    return (sy - b * sx) / n, b


def fit(rows):
    eng = {}
    for r in rows:
        eng.setdefault(r["engine"], r["name"])
    for e, n in GONE.items():
        eng.setdefault(e, n)
    neng = max(eng) + 1
    base = sum(q(r["idle_us"]) for r in rows) / len(rows)
    cost, inter, notes = [0] * neng, {}, []
    missing = []
    for e in range(neng):
        if e in GONE:
            notes.append(f" * {e:2} {eng[e]:9} {0:4} q   (no engine: the number kept, nothing plays it)")
            continue
        pts = [(x_of(r["voices"], r["split"]), q(r["load_us"])) for r in rows
               if r["engine"] == e and r["flags"] == 32 and r["voices"] > 0]
        if not pts:
            missing.append(e)
            continue
        a, b = line(pts)
        if len({p[0] for p in pts}) < 2:                # one voice count (a cap): through the floor
            b = (sum(p[1] for p in pts) / len(pts) - base) / pts[0][0]
            a = None
        cost[e] = max(1, round(b))
        inter[e] = a
        sp = '  split' if any(r['split'] for r in rows if r['engine'] == e) else '       '
        if a is None:
            notes.append(f" * {e:2} {eng[e]:9} {cost[e]:4} q a voice (one core){sp}   (its voice cap: one count, through the floor)")
        else:
            res = max(abs(a + b * x - y) for x, y in pts)
            notes.append(f" * {e:2} {eng[e]:9} {cost[e]:4} q a voice (one core){sp}   floor {a:5.0f}   worst residual {res:4.0f} q")
    measured = sorted(c for i, c in enumerate(cost) if i not in missing and i not in GONE)
    for e in missing:                                   # no voice sounded on the bench (WAVE: its table is loaded by
        cost[e] = measured[(3 * len(measured)) // 4]    # the UI): the upper quartile of the others, on the safe side
        notes.insert(e, f" * {e:2} {eng[e]:9} {cost[e]:4} q a voice (not measured: the upper quartile of the others)")
    lined = [e for e in inter if inter[e] is not None]
    split_e = sorted(e for e in lined if any(r["split"] for r in rows if r["engine"] == e))
    ser_e = sorted(e for e in lined if e not in split_e)
    vc_part = max(0.0, sum(inter[e] - base for e in ser_e) / max(1, len(ser_e)))
    vc_split = max(0.0, sum(inter[e] - base for e in split_e) / max(1, len(split_e)) - vc_part)

    def feat(flag):
        """(slope, intercept) of the runs with flags 32 + flag over the bare line, averaged over the engines"""
        ds, di = [], []
        for e in sorted({r["engine"] for r in rows if r["flags"] == 32 + flag}):
            pts = [(x_of(r["voices"], r["split"]), q(r["load_us"])) for r in rows
                   if r["engine"] == e and r["flags"] == 32 + flag]
            if len({p[0] for p in pts}) < 2 or inter[e] is None:
                continue                                # (a run missing: no line)
            a, b = line(pts)
            ds.append(b - cost[e])
            di.append(a - inter[e])
        return sum(ds) / len(ds), sum(di) / len(di)
    f2 = feat(1)[0]
    side = (feat(2)[0] - f2 + feat(8)[0]) / 2
    slot16 = feat(4)[0] * 16 / 12
    pfx = feat(16)[1]
    allf = feat(31)
    pred_all = f2 + side + slot16 * 12 / 16
    notes.append(f" * every effect at once: {allf[0]:.0f} q a voice over the bare one, the parts summed {pred_all:.0f}")
    return dict(cost=cost, base=round(base), split=round(vc_split), part=round(vc_part), pfx=max(0, round(pfx)),
                f2=max(0, round(f2)), side=max(0, round(side)), slot16=max(0, round(slot16)), notes=notes, eng=eng)


def render(m):
    out = ["/* SPDX-License-Identifier: GPL-3.0-only */",
           "/* generated by tools/gen_vcost.py from tools/vcost_bench.tsv (fm1-emu, the 16-voice bench): the cost of a voice",
           " * for the CPU-pressure admission (voice.c vc_admit), in 1/4096 of the half buffer (5804 us): ENGINES[] order",
           " * (Clipper), a voice on one core, no FILTER 2, no matrix slot, no SPREAD, its heaviest factory preset. The fit:",
           *m["notes"], " */", "#pragma once", "#define VC_Q 4096u", f"#define VC_NENG {len(m['cost'])}u",
           "static const uint16_t VC_ENG[VC_NENG] = {"]
    names = m["eng"]
    for i in range(0, len(m["cost"]), 6):
        part = m["cost"][i:i + 6]
        out.append("    " + " ".join(f"{c:4}," for c in part) + "   /* " + " ".join(names[i + k] for k in range(len(part))) + " */")
    out += ["};",
            f"#define VC_BASE {m['base']}u                   /* the render with no voice (the effects, the UI's share) */",
            f"#define VC_SPLIT {m['split']}u                   /* handing a part's voices to core 1 and waiting, a block */",
            f"#define VC_PART {m['part']}u                    /* a sounding part's own work */",
            f"#define VC_PFX {m['pfx']}u                    /* .. its effects on (DIST, INSERT, a send) */",
            f"#define VC_F2 {m['f2']}u                      /* a voice's FILTER 2 */",
            f"#define VC_SIDE {m['side']}u                    /* a voice's stereo side (SPREAD, FILTER 2 SPLIT) */",
            f"#define VC_SLOT16 {m['slot16']}u                  /* a live matrix slot, in 1/16 */", ""]
    return "\n".join(out)


def load(path):
    rows = []
    for ln in pathlib.Path(path).read_text().splitlines():
        if not ln.strip() or ln.startswith("#"):
            continue
        f = ln.split("\t")
        rows.append(dict(tag=f[0], engine=int(f[1]), name=f[2], preset=int(f[3]), flags=int(f[4]), voices=int(f[5]),
                         split=int(f[6]), idle_us=float(f[7]), load_us=float(f[8])))
    return rows


def main(argv):
    check = "--check" in argv
    args = [a for a in argv if a != "--check"]
    tsv = args[0] if args else ROOT / "tools" / "vcost_bench.tsv"
    hdr = pathlib.Path(args[1] if len(args) > 1 else ROOT / "firmware" / "src" / "vcost_tables.h")
    text = render(fit(load(tsv)))
    if check:
        if hdr.read_text() != text:
            print(f"gen_vcost: {hdr} is not what {tsv} gives: run tools/gen_vcost.py")
            return 1
        print(f"gen_vcost: {hdr.name} matches the bench ({tsv})")
        return 0
    hdr.write_text(text)
    print(f"gen_vcost: {hdr}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
