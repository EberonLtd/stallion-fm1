#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
"""Tune hooks: an engine constant a host build may override to try values (a tuning build
rebuilds its host renderer with -DNAME=value), written in the engine's source as

    #ifndef ROD_RESK
    #define ROD_RESK 8400
    #endif

Checks, for every such hook in firmware/src/eng_*.c (a one-line object-like #define straight after its #ifndef):
  1. the device is never given the value: no build script (build.sh, tools/*.py, *.ps1, *.bat) passes -DNAME
  2. engine-local: NAME is used in its own engine's source only (no other firmware file), so a value tried on one
     engine cannot change another engine's sound (their golden renders stay bit for bit)
  3. the hook is the plain #define to the compiler: the engine's source preprocessed as it is, and with every hook
     block cut down to its bare #define, give the same token stream (the device build compiles the same code, so the
     image is the same: checked byte for byte with build.sh when the hooks were added); and preprocessed again with
     -DNAME=<the default>, the same again (a tuning build's baseline is the device's code)
A hook's value is a plain literal (a number, optionally signed, with a suffix): a tuning tool reads the default and
writes the patch. An #ifndef'd #define whose value is an expression (eng_sample.c SMP_STEP_MAX) is a build switch,
not a hook, and is left alone."""
import glob
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
HOOK = re.compile(r"^#ifndef (\w+)[ \t]*\n#define \1[ \t]+([^\n\\]*?)[ \t]*(/\*[^\n]*)?\n#endif[^\n]*$", re.M)
LIT = re.compile(r"^-?(0x[0-9A-Fa-f]+|\d+(\.\d*)?([eE]-?\d+)?)[uUlLfF]*$")
CC = os.environ.get("CC", "cc").split()[0]


def hooks():
    out = []
    for p in sorted(glob.glob(os.path.join(ROOT, "firmware/src/eng_*.c"))):
        s = open(p, encoding="utf-8").read()
        for m in HOOK.finditer(s):
            if LIT.match(m.group(2).strip()):
                out.append((p, m.group(1), m.group(2).strip(), m))
    return out


def preprocess(path, defs=()):
    inc = [f"-I{os.path.join(ROOT, d)}" for d in ("build/gen", "firmware/src", "firmware/hal")]
    r = subprocess.run([CC, "-E", "-P", "-DCLIPPER_SYNTH=1", *inc, *[f"-D{d}" for d in defs], path], capture_output=True,
                       text=True)
    if r.returncode:
        raise SystemExit(f"cc -E {path}: {r.stderr[:400]}")
    return " ".join(r.stdout.split())


def main():
    fails = []
    hs = hooks()
    if not hs:
        print("no tune hooks found")
        return 1
    builders = [os.path.join(ROOT, "build.sh")] + glob.glob(os.path.join(ROOT, "tools/*.py")) + \
        glob.glob(os.path.join(ROOT, "*.ps1")) + glob.glob(os.path.join(ROOT, "*.bat"))
    btext = {b: open(b, encoding="utf-8", errors="replace").read() for b in builders}
    others = [p for p in glob.glob(os.path.join(ROOT, "firmware/**/*.[ch]"), recursive=True)]
    otext = {p: open(p, encoding="utf-8", errors="replace").read() for p in others}
    for path, name, val, _ in hs:
        rel = os.path.relpath(path, ROOT)
        for b, t in btext.items():
            if re.search(r"-D\s*" + name + r"\b", t):
                fails.append(f"{name}: {os.path.relpath(b, ROOT)} passes -D{name} (a device build must use the default)")
        for p, t in otext.items():
            if os.path.abspath(p) != os.path.abspath(path) and re.search(r"\b" + name + r"\b", t):
                fails.append(f"{name} ({rel}): also used in {os.path.relpath(p, ROOT)} (a hook must be engine-local)")
    with tempfile.TemporaryDirectory() as td:
        for path in sorted({h[0] for h in hs}):
            s = open(path, encoding="utf-8").read()
            bare = HOOK.sub(lambda m: f"#define {m.group(1)} {m.group(2)}" if LIT.match(m.group(2).strip()) else m.group(0), s)
            bp = os.path.join(td, os.path.basename(path))
            open(bp, "w", encoding="utf-8").write(bare)
            a, b = preprocess(path), preprocess(bp)
            if a != b:
                fails.append(f"{os.path.relpath(path, ROOT)}: the hooks change the preprocessed code")
            mine = [(n, v) for p, n, v, _ in hs if p == path]
            c = preprocess(path, [f"{n}={v}" for n, v in mine])
            if c != a:
                fails.append(f"{os.path.relpath(path, ROOT)}: -D with the defaults changes the preprocessed code")
    for path, name, val, _ in hs:
        print(f"  hook {os.path.relpath(path, ROOT)} {name} = {val}")
    for f in fails:
        print("FAIL", f)
    print(f"{len(hs)} tune hooks, {len(fails)} failures")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
