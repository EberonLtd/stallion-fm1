# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
"""Scratch-only CPU bench hooks (v2, load-steal) applied to a copy of the firmware tree (argv[1]).
MIDI program change, bank b (CC 0), program p:
  b = 127  -> reset the counters and start the per-half trace
  b = 125  -> EDIT route = p (applied by the next engine program change): 0 none, 1 LFO 1 -> E1 in slot 12
  b = 126  -> feature flags = p (applied by the next engine program change):
              bit0-1 FILTER 2 (0 off, 1 SER, 2 SPLIT), bit2 12 matrix slots (no EDIT-backed route),
              bit3 SPREAD + VINTAGE, bit4 part FX (DIST, sends, INSERT, shimmer), bit5 POLY (else UNI4),
              bit6 the preset as it is (none of the above cleared)
  b < 64   -> engine b, preset p, with the flags
Shedding off unless BENCH_SHED=1. Trace: per half, render us | voices << 16 | late << 24 (TRN halves)."""
import sys, pathlib

root = pathlib.Path(sys.argv[1])
src = root / "firmware" / "src"

ui = src / "ui_synth.c"
t = ui.read_text()
old = """        midi_pc_req = 0;
        base_pos(&total);
        if (pc - 1u < total)
            preset_go(fav_list(b) + pc - 1u);"""
assert old in t
t = t.replace(old, """        midi_pc_req = 0;
        (void)b, (void)total;
        bench_pc(pc - 1u);""")
hook = r'''
/* ---- scratch-only CPU bench (not for release) ---- */
static uint32_t bench_flags, bench_edit;
UI_SMALL static void bench_pc(uint32_t n)
{
    uint32_t bank = n >> 7, prog = n & 127u, i, k, f = bench_flags;
    track_t *t = TSEL;
    if (bank == 127u) {
        felucca_dbg.max_us = 0;
        diag_peak = 0;
        audio_max_us = 0;
        felucca_dbg.late = 0;
        bench_st.vmax = 0;
        bench_st.resets++;
        bench_st.kills0 = voice_kills;
        bench_st.shed0 = shed_count;
        bench_st.tn = 0;
        return;
    }
    if (bank == 126u) {
        bench_flags = prog;
        return;
    }
    if (bank == 125u) {
        bench_edit = prog;
        return;
    }
    select_engine(bank & 63u);
    apply_preset(prog & 31u);
    t->p[P_VOICE] = (f & 32u) ? V_POLY : V_PUNI4;
    if (!(f & 64u)) {
        t->p[P_F2MODEL] = 0;
        for (k = 0; k < MX_SLOTS; k++)
            t->p[mx_pid(k)] = 0, t->p[mx_pid(k) + 1u] = 0;
        t->p[P_SPREAD] = 0, t->p[P_VINT] = 0;
        t->p[P_DIST] = 0, t->p[P_CHOR] = 0, t->p[P_DLY] = 0, t->p[P_REV] = 0, t->p[P_ITYPE] = 0;
    }
    if (f & 3u)
        t->p[P_F2MODEL] = 2, t->p[P_F2ROUTE] = (f & 3u) == 2u ? 2 : 0, t->p[P_F2CUT] = 70, t->p[P_F2RES] = 60;
    if (f & 4u) {
        static const uint8_t SRC[12] = {MS_LFO1, MS_LFO2, MS_ENV2, MS_KEY, MS_VEL, MS_RAND, MS_LFO3, MS_MW, MS_AT,
                                        MS_ENV1, MS_LFO1, MS_LFO2};
        static const uint8_t DST[12] = {MT_PITCH, MT_CUT, MT_SHAPE, MT_AMP, MT_PAN, MT_CUT2, MT_RES2, MT_PIT1, MT_PIT2,
                                        MT_CUT, MT_AMP, MT_L2RATE};
        for (k = 0; k < MX_SLOTS; k++) {
            i = mx_pid(k);
            t->p[i] = SRC[k], t->p[i + 1u] = DST[k], t->p[i + 2u] = 12;
        }
    }
    if (f & 8u)
        t->p[P_SPREAD] = 127, t->p[P_VINT] = 64;
    if (bench_edit & 1u) {
        i = mx_pid(MX_SLOTS - 1u);
        t->p[i] = MS_LFO1, t->p[i + 1u] = MT_E1, t->p[i + 2u] = 30;
    }
    if (f & 16u) {
        t->p[P_DIST] = 40, t->p[P_CHOR] = 100, t->p[P_DLY] = 100, t->p[P_REV] = 100;
        t->p[P_ITYPE] = 14, t->p[P_IMIX] = 100;
        song.g[G_RSHIM] = 64;
    }
}
'''
anchor = "/* once a UI frame: MIDI CC 20 moves MORPH */"
assert anchor in t
t = t.replace(anchor, hook + anchor)
ui.write_text(t)

vc = src / "voice.c"                                   # the cost bench (shedding off): no admission either, every
v = vc.read_text()                                      # note sounds (the before / after builds, BENCH_SHED=1: as built)
old = "#define VC_BUDGET (VC_Q * 80u / 100u)"
if old in v:
    vc.write_text(v.replace(old, "#if BENCH_SHED\n" + old + "\n#else\n#define VC_BUDGET (VC_Q * 64u)\n#endif"))

au = src / "audio.c"
a = au.read_text()
old = "                shed_req = 1;                           /* two in a row */"   # (shed_judge since steal-quiet-tails)
assert old in a
a = a.replace(old, "#if BENCH_SHED\n" + old + "\n#else\n                ;\n#endif")
a = "#ifndef BENCH_SHED\n#define BENCH_SHED 0\n#endif\n" + a
if "vc_half(us)" in a:
    a = "#define VC_BENCH (bench_st.allowed = vc_fit(&trk[0]), bench_st.ratio = vc_ratio)\n" + a
old = "static volatile uint32_t audio_halves, audio_max_us;"
assert old in a
a = a.replace(old, old + """
#define TRN 200u
struct bench_st { uint32_t magic, vmax, vnow, kills, shed, resets, kills0, shed0, allowed, ratio, tn, pad;
                  uint32_t tr[TRN]; } bench_st;""")
old = "        if (us > audio_max_us)\n            audio_max_us = us;"
assert old in a
a = a.replace(old, old + """
        {
            uint32_t bp, bi, bn = 0;
            for (bp = 0; bp < NPART; bp++)
                for (bi = 0; bi < NVOICE; bi++)
                    bn += trk[bp].v[bi].active && trk[bp].v[bi].stage != 4u;
            bench_st.magic = 0x424E4348u;
            bench_st.vnow = bn;
            if (bn > bench_st.vmax)
                bench_st.vmax = bn;
            bench_st.kills = voice_kills - bench_st.kills0, bench_st.shed = shed_count - bench_st.shed0;
#ifdef VC_BENCH
            VC_BENCH;
#endif
            if (bench_st.tn < TRN)
                bench_st.tr[bench_st.tn++] = us | bn << 16 | (uint32_t)(fm1_audio_free_half() != half) << 24 | (uint32_t)(lim_dac.g >> 9 > 127 ? 127 : lim_dac.g >> 9) << 25;
        }""")
au.write_text(a)

bp = root / "tools" / "build.py"
b = bp.read_text()
for old in ('"NVOICE", "VS_CHECK", "QUIET_MANY"):', '"NVOICE", "VS_CHECK"):'):
    if old in b:
        b = b.replace(old, old[:-2] + ', "BENCH_SHED"):')
        break
else:
    raise SystemExit("build.py anchor")
bp.write_text(b)
print("patched", root)
