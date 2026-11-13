#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
"""The factory bank files presets-bank-<engine>.txt (in the repository's root) from the firmware itself: each engine's
preset table in its order (tools/site_counts.c --presets, the synth build: CLIPPER_SYNTH 1) and each sound's kind from
firmware/src/ui.c BANK[] (the synth's part, before the groovebox's #else). One file per engine with presets:
  # <ENGINE> factory bank: index, name, BANK[] category
  index<TAB>name<TAB>BK_x
  python3 tools/gen_bank_files.py            write them (and remove the file of an engine that has none any more)
  python3 tools/gen_bank_files.py --check    exit 1 when a file is not what the firmware says
Needs build/gen (a firmware build or the test suite's)."""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "build" / "host" / "site_counts"


def presets():
    EXE.parent.mkdir(parents=True, exist_ok=True)
    cc = os.environ.get("CC", "cc").split()
    subprocess.run(cc + ["-O1", "-w", "-DCLIPPER_SYNTH=1", "-Ibuild/gen", "-Ifirmware/src", "-Ifirmware/hal",
                         "tools/site_counts.c", "-lm", "-o", str(EXE)], cwd=ROOT, check=True)
    out = subprocess.run([str(EXE), "--presets"], check=True, capture_output=True, text=True).stdout
    rows = []
    for ln in out.splitlines():
        e, en, i, name = ln.split("\t")
        rows.append((int(e), en, int(i), name))
    return rows


def bank():
    """(engine, name) -> kind, as ui.c bank_resolve finds them (by name in the engine's table)"""
    core = (ROOT / "firmware/src/core.h").read_text()
    engi = {m.group(1): int(m.group(2)) for m in re.finditer(r"#define (ENGI_\w+) (\d+)u", core)}
    ui = (ROOT / "firmware/src/ui.c").read_text()
    i = ui.index("static const struct { uint8_t kind, e; const char *name; } BANK[] = {")
    j = ui.index("#else", i)                          # (the synth's list: the groovebox's follows the #else)
    kind = {}
    for m in re.finditer(r'\{(BK_\w+), (\w+), "([^"]*)"\}', ui[i:j]):
        e = int(m.group(2)) if m.group(2).isdigit() else engi[m.group(2)]
        kind[(e, m.group(3))] = m.group(1)
    return kind


def files():
    kind, out = bank(), {}
    for e, en, i, name in presets():
        f = ROOT / f"presets-bank-{en.lower()}.txt"
        if f not in out:
            out[f] = [f"# {en} factory bank: index, name, BANK[] category"]
        k = kind.get((e, name))
        if not k:
            raise SystemExit(f"gen_bank_files: {en} {name} is in no BANK[] kind (firmware/src/ui.c)")
        out[f].append(f"{i}\t{name}\t{k}")
    return {f: "\n".join(lines) + "\n" for f, lines in out.items()}


def main(argv):
    check = "--check" in argv
    want = files()
    stale = sorted(f for f in ROOT.glob("presets-bank-*.txt") if f not in want)
    bad = sorted(f for f, t in want.items() if not f.exists() or f.read_text() != t)
    if check:
        for f in bad + stale:
            print(f"gen_bank_files: {f.name} is not what the firmware says: run tools/gen_bank_files.py")
        if not bad and not stale:
            print(f"gen_bank_files: {len(want)} bank files match the firmware")
        return 1 if bad or stale else 0
    for f in bad:
        f.write_text(want[f])
    for f in stale:
        f.unlink()
    print(f"gen_bank_files: {len(bad)} written, {len(stale)} removed, {len(want)} in all")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
