#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Target-side cost estimate of the hot DSP code, from the pi32v2 objdump listing tools/build.py writes
(build/felucca.dis). For each engine's render function and the audio ISR: the instructions
inside loops (every address range closed by a backward branch: the per-sample loops and what they
contain; a backward branch the code at its target cannot run back to is no loop: Clipper 1.3, the compiler's
jumps back to a shared return or to a join after a block it put out of line had counted whole functions as
loops: SAMPLE +356 %, GRAIN +37 %), and the hardware divides and calls in them. cost = the loop instructions, each weighted 4 per
level of nesting (an inner loop runs several times per sample) and 1 + 8 for a divide (many cycles). A static count, not a cycle count: it changes only when the compiled code changes,
so it is exact run to run, and it catches a render loop that grew (more work per sample, a new divide,
something not inlined any more). Compared with tests/target_budget.txt at +10 %. The host
CPU check (regress.c) is the primary one; this one sees the target compiler's code.
  tests/target_budget.py [DIS [BUDGET]]      BUDGET_UPDATE=1 rewrites BUDGET"""
import os
import re
import sys

FUNCS = ["analog_render", "digital_render", "phase_render", "sample_render", "formant_render",
         "trio_render", "trio_pass", "drawbar_render", "drawbar_block",
         "grain_render", "grain_block", "slicer_track",   # (drums_mix: Clipper 1.2, no drum machine on the FM-1)
         "fm6_op_run", "fm6_op_fb", "fm6_render",     # FM6 (SLOOP 2.4): the operator loops (noinline), the voice
         "audio_block", "fm1_alnk0_irq",              # the mix (audio.c, noinline: 1.3), the ISR around it
         "palomino_render", "stampede_render", "rodeo_render",   # (Stallion) its engines; MARE's VCOs and ladders are
         "mare_render", "mar_vco", "mar_lp",           # loops of their own (mar_vco: a block of one VCO, 3 a block)
         "mtf_set", "mar_lp_fm", "mar_hp_fm", "mar_mix_ring", "mar_mix_spl",   # MARE FILTER FM, RING, SPLT
         "mar_out_split", "mar_mods", "mare_block", "mar_split", "mar_post", "mar_ac",   # ST/SP, the VCOs' offsets, the part's block
         "ins_sdly",                                  # .. the stereo analog delay (INSERT STDLY / PPDLY), a part's
         "pony_render",                               # PONY (its panel's RANGE KYBD VCA STEPS MODL: per block)
         "pony_chorus", "pony_hpf"]                   # .. its chorus (a part's; CHAR's HISS), CHAR's HPF after the sum
TOL = 0.10                      # exact (no noise): small edits pass, a grown render loop does not
DIV_W = 8                       # a divide weighs 1 + 8 instructions
NEST = 4                        # an instruction in a loop inside a loop weighs 4, two deep 16, ...
MAXD = 4

dis = sys.argv[1] if len(sys.argv) > 1 else "build/felucca.dis"
budget = sys.argv[2] if len(sys.argv) > 2 else "tests/target_budget.txt"
LABEL = re.compile(r"^([A-Za-z_][A-Za-z_0-9.]*):$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")
TARGET = re.compile(r"goto -?\d+ <[^>]*: ([0-9a-f]+) >")
RET = re.compile(r"^(\{pc\b[^}]*\}|pc) = \[sp\+\+\]|^rt[si]\b")
JTV = re.compile(r"^< (\d+) : 0x[0-9A-Fa-f]+ >$")
JT = {}


def functions(path):
    """{name: [(addr, text)]}: a function runs to the next label not starting with '.' (the compiler's
    jump-table labels are inside functions); jump-table data ('< n : 0x.. >') is left out of it, read into
    JT: {tbb / tbh address: the addresses it jumps to (the table's start + 2 x n)}"""
    out, cur, tb = {}, None, None
    with open(path) as f:
        for line in f:
            m = LABEL.match(line.strip())
            if m:
                name = m.group(1)
                if not name.startswith("."):
                    cur = out.setdefault(name, []) if name in FUNCS else None
                continue
            m = INSN.match(line)
            if cur is None or not m:
                continue
            a, t = int(m.group(1), 16), m.group(3).strip()
            if not t.startswith("<"):
                cur.append((a, t))
                tb = [a, None] if t.startswith(("tbb", "tbh")) else None
            elif tb and JTV.match(t):
                tb[1] = a if tb[1] is None else tb[1]
                if int(JTV.match(t).group(1)):                  # (0: the byte that pads a tbb table)
                    JT.setdefault(tb[0], []).append(tb[1] + 2 * int(JTV.match(t).group(1)))
    return out


def reach(insns, h):
    """the addresses the code at h can run to: falls through, branches (if ... goto: both ways), stops at a
    return ({pc, ..} = [sp++], rts); a jump table (tbb, tbh) to its entries (JT; anywhere if not read)"""
    addr = [a for a, _ in insns]
    at = {a: k for k, a in enumerate(addr)}
    seen, todo = set(), [h]
    while todo:
        a = todo.pop()
        if a in seen or a not in at:
            continue
        seen.add(a)
        k = at[a]
        t = insns[k][1]
        if t.startswith(("tbb", "tbh")):
            todo += JT.get(a, addr)
            continue
        m = TARGET.search(t)
        if m:
            todo.append(int(m.group(1), 16))
        cond = k and insns[k - 1][1].endswith("{")       # (ifs (..) { : the next instruction runs or not)
        if not cond and (t.startswith("goto") or RET.match(t)):
            continue
        if k + 1 < len(addr):
            todo.append(addr[k + 1])
    return seen


def cost(insns):
    """loops: one span per loop head (the farthest backward branch to it that the head's code runs back to:
    the compiler also jumps back to a shared return or to a join after a block it put out of line, no loop);
    an instruction inside d spans weighs NEST ** (d - 1) (an inner loop runs several times per pass of the
    outer one)"""
    lo = insns[0][0]
    heads, runs = {}, {}
    for a, t in insns:
        m = TARGET.search(t)
        if m and lo <= int(m.group(1), 16) < a:
            h = int(m.group(1), 16)
            if h not in runs:
                runs[h] = reach(insns, h)
            if a in runs[h]:
                heads[h] = max(heads.get(h, a), a)
    n = divs = calls = 0
    w = 0
    for a, t in insns:
        d = min(sum(1 for h, e in heads.items() if h <= a <= e), MAXD)
        if not d:
            continue
        k = NEST ** (d - 1)
        n += 1
        if re.search(r"= r\d+ / r\d+", t):
            divs += 1
            k *= 1 + DIV_W
        calls += t.startswith("call")
        w += k
    return {"insns": len(insns), "loop": n, "div": divs, "call": calls, "cost": w}


def main():
    if not os.path.exists(dis):
        print(f"target: skip ({dis} missing: run ./build.sh)")
        return 0
    fns = functions(dis)
    res = {n: cost(fns[n]) for n in FUNCS if fns.get(n)}
    missing = [n for n in FUNCS if n not in res]
    base = {}
    if os.path.exists(budget):
        for line in open(budget):
            p = line.split()
            if len(p) >= 2 and not line.startswith("#"):
                base[p[0]] = int(p[1])
    if os.environ.get("BUDGET_UPDATE"):
        with open(budget, "w") as f:
            f.write("# FELUCCA target cost budget (tests/target_budget.py): instructions in the loops of each\n"
                    f"# function in build/felucca.dis, x{NEST} per nesting level, divides x{1 + DIV_W}. The check allows "
                    f"+{TOL * 100:.0f} %.\n# Rewritten by BUDGET_UPDATE=1.\n")
            for n, r in res.items():
                f.write(f"{n} {r['cost']}\n")
        print(f"target: budget {budget} rewritten ({len(res)} functions)")
    fail = 0
    for n in missing:
        print(f"target: FAIL {n} not found in {dis} (renamed? inlined? update FUNCS)")
        fail += 1
    for n, r in res.items():
        b = base.get(n)
        state = "no budget (BUDGET_UPDATE=1 adds it)" if b is None else "ok"
        if b is not None and not os.environ.get("BUDGET_UPDATE"):
            if r["cost"] > b * (1 + TOL):
                state = f"OVER BUDGET (+{(r['cost'] / b - 1) * 100:.0f} %, limit +{TOL * 100:.0f} %)"
                fail += 1
            elif r["cost"] < b * (1 - TOL):
                state = f"note: {(r['cost'] / b - 1) * 100:.0f} % (BUDGET_UPDATE=1 to keep it)"
        print(f"target: {n:15s} {r['insns']:5d} instructions, {r['loop']:4d} in loops, {r['div']} divides, "
              f"{r['call']:2d} calls there: cost {r['cost']:5d} (budget {b if b is not None else '-'}) {state}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
