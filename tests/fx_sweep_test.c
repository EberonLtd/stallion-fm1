/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 3dSam (Clipper) */
/* Clipper 1.0 (comments: a crackle on the left with PLATE, HALL, SHIMMER: a left output tap read outside its line).
 * A sine (440 Hz, 3 kHz, faded in) through every effect at its extremes, each from a clean line: every reverb type
 * (SIZE, PRE, DAMP), the delay (each MODE, each TIME, FDBK 0 and 120), the chorus (RATE, DEPTH), the INSERT's swept
 * types (A, B, C; 1.1: and PITCH, PIT2, PHAS6, DELAY, LOCMP without its LOFI), the PONY chorus, the master (EQ, COMP,
 * FILT). A linear effect on a sine leaves no corner: its largest second difference stays within 12 x its rms, and
 * neither side is 4 x the other. 1.1: the chorus's RATE jumped at 16 phases of its LFO (no corner beyond the steady
 * sound's). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define NB 2400u                         /* (release-1.0: 1.74 s, was 1.16: PING's right echo comes at 2 x TIME, 1.56 s at
                                          * 780MS; the delay judged over the last 300 blocks, below) */
static int32_t L[NB * CTL], R[NB * CTL];
static double fq = 440.0;
static int32_t sine(uint32_t s, int32_t a) { double e = s < 4000u ? s / 4000.0 : 1.0; return (int32_t)(a * e * e * sin(2 * M_PI * fq * s / 44100.0)); }
static int bad;
/* (1.1) the chorus alone on the sine, nb blocks on from the sample cs: the largest second difference of either side
 * (across the blocks' joints: the corner of a change at its block's first sample counts) */
static uint32_t cs;
static int32_t chorus_d2(uint32_t nb)
{
    static int32_t ci[CTL], z[CTL], wl[CTL], wr[CTL], pl[2], pr[2];
    int32_t mx = 0, a, c;
    uint32_t b, i;
    for (b = 0; b < nb; b++) {
        for (i = 0; i < CTL; i++) z[i] = 0, ci[i] = sine(cs++, 20000);
        fx_buses(ci, z, z, wl, wr, CTL);
        for (i = 0; i < CTL; i++) {
            a = wl[i] - 2 * pl[1] + pl[0], c = wr[i] - 2 * pr[1] + pr[0];
            pl[0] = pl[1], pl[1] = wl[i], pr[0] = pr[1], pr[1] = wr[i];
            if (abs(a) > mx) mx = abs(a); if (abs(c) > mx) mx = abs(c);
        }
    }
    return mx;
}
static void judge(const char *what, uint32_t from)
{
    double sl = 0, sr = 0; int32_t ml = 0, mr = 0; uint32_t i, n = NB * CTL, c = 0;
    for (i = from * CTL + 2; i < n; i++) {
        int32_t a = L[i] - 2 * L[i - 1] + L[i - 2], b = R[i] - 2 * R[i - 1] + R[i - 2];
        sl += (double)a * a, sr += (double)b * b, c++;
        if (abs(a) > ml) ml = abs(a); if (abs(b) > mr) mr = abs(b);
    }
    sl = sqrt(sl / c) + 1, sr = sqrt(sr / c) + 1;
    if (ml > 12 * sl || mr > 12 * sr || (sl > 4 * sr && sl > 50) || (sr > 4 * sl && sr > 50)) {
        bad++;
        printf("%-44s L %5.1f x (rms %6.0f)  R %5.1f x (rms %6.0f)\n", what, ml / sl, sl, mr / sr, sr);
    }
}
int main(int argc, char **argv)
{
    static int32_t ci[CTL], di[CTL], ri[CTL], wl[CTL], wr[CTL], b0[CTL];
    uint32_t i, b, k, cases = 0;
    char w[96];
    host_tracks_init();
    for (i = 0; i < NPART; i++) host_preset(&trk[i], i ? 1u : ENGI_HYBRID, 0);
    clipper_proj_neutral(); trk[1].p[P_LYMODE] = LY_OFF; song.g[G_BPM] = 120;
    for (k = 0; k < 2; k++, fq = 3000.0) {
        int rt, sz, pre, dmp, dt, md, fb, cr, cd;
        /* reverbs */
        for (rt = 0; rt < 4; rt++) for (sz = 0; sz <= 127; sz += 127) for (pre = 0; pre <= 100; pre += 100) for (dmp = 0; dmp <= 127; dmp += 127) {
            song.g[G_RTYPE] = rt, song.g[G_RSIZE] = sz, song.g[G_RPRE] = pre, song.g[G_RDAMP] = dmp;
            for (b = 0; b < NB; b++) {
                for (i = 0; i < CTL; i++) ci[i] = di[i] = 0, ri[i] = sine(b * CTL + i, 20000);
                fx_buses(ci, di, ri, wl, wr, CTL);
                for (i = 0; i < CTL; i++) L[b * CTL + i] = wl[i], R[b * CTL + i] = wr[i];
            }
            snprintf(w, sizeof w, "%.0f Hz reverb %d size %d pre %d damp %d", fq, rt, sz, pre, dmp); judge(w, 800); cases++;
        }
        song.g[G_RTYPE] = 0, song.g[G_RSIZE] = 90, song.g[G_RPRE] = 0, song.g[G_RDAMP] = 60;
        /* delay */
        for (md = 0; md < 3; md++) for (dt = GP[G_DTIME].min; dt <= GP[G_DTIME].max; dt++) for (fb = 0; fb <= 120; fb += 120) {
            song.g[G_DMODE] = md, song.g[G_DTIME] = dt, song.g[G_DFDBK] = fb;
            for (i = 0; i < 4 * CTL; i++) { ci[i % CTL] = ri[i % CTL] = di[i % CTL] = 0; }
            for (b = 0; b < 80; b++) fx_buses(ci, di, ri, wl, wr, CTL);   /* (the TIME / MODE fades) */
            memset(dly_buf, 0, sizeof dly_buf); fx.dly_lp = fx.dly_acc = 0;
            for (b = 0; b < NB; b++) {
                for (i = 0; i < CTL; i++) ci[i] = ri[i] = 0, di[i] = sine(b * CTL + i, 20000);
                fx_buses(ci, di, ri, wl, wr, CTL);
                for (i = 0; i < CTL; i++) L[b * CTL + i] = wl[i], R[b * CTL + i] = wr[i];
            }
            snprintf(w, sizeof w, "%.0f Hz delay mode %d time %d fb %d", fq, md, dt, fb);
            judge(w, NB - 300u); cases++;               /* (after the TIME / MODE fades, the line filled for short times;
                                                         * release-1.0: from block 2100, 1.52 s. Judged from 1300, 0.94 s,
                                                         * to 1.16 s, PING at 600MS 700MS 780MS had no right echo yet (it
                                                         * comes at 1.2 .. 1.56 s): R silent, "one side 4 x the other",
                                                         * 12 cases flagged, on 228b062 and every branch merged here) */
        }
        song.g[G_DMODE] = 0, song.g[G_DTIME] = 1, song.g[G_DFDBK] = 60;
        /* chorus */
        for (cr = 0; cr <= 127; cr += 127) for (cd = 0; cd <= 127; cd += 127) {
            song.g[G_CRATE] = cr, song.g[G_CDEPTH] = cd;
            for (b = 0; b < NB; b++) {
                for (i = 0; i < CTL; i++) di[i] = ri[i] = 0, ci[i] = sine(b * CTL + i, 20000);
                fx_buses(ci, di, ri, wl, wr, CTL);
                for (i = 0; i < CTL; i++) L[b * CTL + i] = wl[i], R[b * CTL + i] = wr[i];
            }
            snprintf(w, sizeof w, "%.0f Hz chorus rate %d depth %d", fq, cr, cd); judge(w, 800); cases++;
        }
        song.g[G_CRATE] = 40, song.g[G_CDEPTH] = 60;
        /* inserts (mono: L = R = the insert's output; the click test only) */
        {
            int ty, a, bb, c;
            static ins_t s;
            int16_t p[P_COUNT];
            for (ty = 1; ty < IT_N; ty++) for (a = 0; a <= 127; a += 127) for (bb = 0; bb <= 127; bb += 127) for (c = 0; c <= 127; c += 127) {
                if (ty <= IT_CRUSH) continue;           /* (the drives: not linear, a sine gets corners by design) */
                if (ty >= IT_PONY1 && ty <= IT_PONY12) continue;   /* (the PONY chorus: below) */
                if (ty == IT_LOCOMP && bb) continue;    /* (1.1: LOCMP's LOFI is CRUSH's: corners by design) */
                memset(&s, 0, sizeof s); memset(p, 0, sizeof p);
                p[P_ITYPE] = ty, p[P_IMIX] = 127, p[P_IA] = a, p[P_IB] = bb, p[P_IC] = c;
                for (b = 0; b < NB; b++) {
                    for (i = 0; i < CTL; i++) b0[i] = sine(b * CTL + i, 20000);
                    ins_run(&s, p, b0, CTL, 1);
                    for (i = 0; i < CTL; i++) L[b * CTL + i] = R[b * CTL + i] = b0[i];
                }
                snprintf(w, sizeof w, "%.0f Hz insert %d A %d B %d C %d", fq, ty, a, bb, c); judge(w, 400); cases++;
            }
        }
        /* the PONY chorus (as the INSERT's PONY1..JUN12) */
        {
            int ty;
            track_t *t = &trk[0];
            for (ty = IT_PONY1; ty <= IT_PONY12; ty++) {
                t->p[P_ITYPE] = ty, t->p[P_IMIX] = 127, t->p[P_IA] = 127, t->p[P_IB] = 127, t->p[P_IC] = 0;
                for (b = 0; b < NB; b++) {
                    for (i = 0; i < CTL; i++) b0[i] = sine(b * CTL + i, 20000);
                    pony_chorus(t, b0, CTL);
                    for (i = 0; i < CTL; i++) L[b * CTL + i] = b0[i] + pch_side[i], R[b * CTL + i] = b0[i] - pch_side[i];
                }
                snprintf(w, sizeof w, "%.0f Hz PONY chorus %d", fq, ty - IT_PONY1 + 1); judge(w, 800); cases++;
            }
            t->p[P_ITYPE] = 0;
        }
    }
    {   /* (1.1) the chorus's RATE jumped (5 <-> 120, DEPTH 127) at 16 phases of its LFO. The taps move at the LFO's
         * slope; a jump turned their speed at once: a corner in the wet sine, as large as DEPTH x the change of rate x the
         * slope there (none at the LFO's peaks), so clean_test's case (one phase, the one the blocks before it left)
         * failed or passed by chance. The largest second difference in the 8 blocks from the jump within 1.5 x the
         * largest of the steady sound at either rate (at 120: 4 turns of the LFO) */
        int32_t mk[2], st[3], worst = 0;
        uint32_t q, nb = 0;
        fq = 440.0, cs = 0, song.g[G_CDEPTH] = 127;
        for (q = 0; q < 16u; q++) {
            song.g[G_CRATE] = 5, fx.cho_ph = q << 28;  /* (the taps jump there: let it pass) */
            chorus_d2(300);
            st[0] = chorus_d2(100);
            song.g[G_CRATE] = 120, mk[0] = chorus_d2(8);
            chorus_d2(200);
            st[1] = chorus_d2(150);
            song.g[G_CRATE] = 5, mk[1] = chorus_d2(8);
            chorus_d2(200);
            st[2] = chorus_d2(100);
            if (getenv("VERBOSE"))
                printf("chorus RATE jump, phase %2u/16: 5 %d, up %d, 120 %d, down %d, 5 %d\n", q, st[0], mk[0], st[1], mk[1], st[2]);
            if (2 * mk[0] > 3 * (st[0] > st[1] ? st[0] : st[1]))
                nb++, worst = mk[0] > worst ? mk[0] : worst;
            if (2 * mk[1] > 3 * (st[1] > st[2] ? st[1] : st[2]))
                nb++, worst = mk[1] > worst ? mk[1] : worst;
        }
        song.g[G_CRATE] = 40, song.g[G_CDEPTH] = 60;
        cases++;
        if (nb) {
            bad++;
            printf("440 Hz chorus RATE jumped 5 <-> 120 at 16 phases: %u of 32 jumps a corner (worst %d)\n", nb, worst);
        }
    }
    {   /* the master: EQ + COMP (bus_process), FILT (djf_process), at their extremes */
        int lo, hi, cp, fl;
        for (k = 0; k < 2; k++) for (lo = -12; lo <= 12; lo += 24) for (hi = -12; hi <= 12; hi += 24) for (cp = 0; cp <= 127; cp += 127) for (fl = -64; fl <= 63; fl += 127) {
            fq = k ? 3000.0 : 440.0;
            song.g[G_EQLOW] = (int16_t)(EQ_LOW_C + lo), song.g[G_EQHIGH] = (int16_t)(EQ_HIGH_C + hi), song.g[G_COMP] = cp, song.g[G_FILT] = fl;
            for (b = 0; b < NB; b++) {
                for (i = 0; i < CTL; i++) wl[i] = sine(b * CTL + i, 20000), wr[i] = sine(b * CTL + i, 20000);
                djf_process(wl, wr, CTL);
                bus_process(wl, wr, CTL);
                for (i = 0; i < CTL; i++) L[b * CTL + i] = wl[i], R[b * CTL + i] = wr[i];
            }
            snprintf(w, sizeof w, "%.0f Hz master EQ %d %d COMP %d FILT %d", fq, lo, hi, cp, fl); judge(w, 800); cases++;
        }
        song.g[G_EQLOW] = EQ_LOW_C, song.g[G_EQHIGH] = EQ_HIGH_C, song.g[G_COMP] = 0, song.g[G_FILT] = 0;
    }
    printf("fx sweep: %d of %u cases flagged\n", bad, cases);
    return bad ? 1 : 0;
}
