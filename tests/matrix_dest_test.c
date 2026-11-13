/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* Matrix patching, the per-part destinations (CLIPPER_SYNTH 1): ARP, DLY / REV / CHO, LFO1 and E1AMT.
 *   DLY      VEL -> DLY +63 on part 1: the slot raises a velocity-127 note's delay return more than twice as much as
 *            a velocity-20 note's (each against the same note without the slot: velocity alone already makes the
 *            louder note's echo louder); part 2's send unchanged; REV and CHO take the same sums
 *   ARP      the arp at 1/8, +V -> ARP +63: steps at 1/32 (4x as many in the same time); +V -> ARP -63: 1/4
 *   LFO1     MW -> LFO1 +63: LFO 1 cycles faster with the wheel at 127 than at 0
 *   E1AMT    ENV DEST FLT 60, +V -> E1AMT -64: ENV 1 no longer opens the filter (the centroid within 5 % of FLT 0's)
 *   24       twelve cables in slots 13..24 render bit for bit as in 1..12; with 24 live, all are read and heard
 * With no slot to them the new destinations change nothing: the golden renders (run_tests.sh) stay bit for bit */
#define main hostsim_main
#include <stddef.h>
#include "hostsim.c"
#undef main

static int fails;
static void ok(int c, const char *what)
{
    printf("mxdest: %-100s %s\n", what, c ? "ok" : "FAIL");
    fails += !c;
}

#define NCAP 4096u
static int32_t cl[NCAP];
static uint32_t ncap;
static double esum;                                     /* the energy of L + R over the last run */
static void run(uint32_t nb)
{
    static int32_t o[CTL * 2];
    uint32_t b, i;
    ncap = 0;
    esum = 0;
    for (b = 0; b < nb; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            double s = (double)o[2 * i] + o[2 * i + 1];
            esum += s * s;
            if (ncap < NCAP)
                cl[ncap++] = o[2 * i];
        }
    }
}
static void quiet(void)
{
    uint32_t i, k;
    for (i = 0; i < NPART; i++) {
        trk_all_off(&trk[i]);
        trk[i].p[P_AMODE] = 0;
    }
    run(400);
    for (i = 0; i < NPART; i++)
        for (k = 0; k < NVOICE; k++)
            trk[i].v[k].active = trk[i].v[k].gate = 0, trk[i].v[k].stage = 0, trk[i].v[k].env = trk[i].v[k].env_out = 0;
    run(3000);                                          /* (the delay's tail gone) */
}
static void plain(track_t *t)
{
    uint32_t k;
    host_preset(t, 0, 0);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    t->p[P_LD_PIT] = t->p[P_LD_FLT] = t->p[P_LD_SHP] = t->p[P_LD_AMP] = t->p[P_ED_PIT] = t->p[P_ED_FLT] = 0;
    t->p[P_ED_SHP] = 0;
    t->p[P_ATK] = 0, t->p[P_DEC] = 0, t->p[P_SUS] = 127, t->p[P_REL] = 0;
    t->p[P_VOICE] = V_POLY, t->p[P_PAN] = 0, t->p[P_GLIDE] = 0;
    t->p[P_MWDST] = t->p[P_ATDST] = MD_OFF;
    t->p[P_AMODE] = 0, t->p[P_VINT] = 0, t->p[P_FMODEL] = FM_SVF, t->p[P_VELF] = 0;
    t->p[P_ITYPE] = 0, t->p[P_IMIX] = 127;
    for (k = 0; k < MX_SLOTS; k++)
        t->p[mx_pid(k)] = t->p[mx_pid(k) + 1u] = t->p[mx_pid(k) + 2u] = 0;
    for (k = P_SPREAD; k < P_E0; k++)
        t->p[k] = TP[k].def;
}
static void slot(track_t *t, uint32_t k, uint32_t src, uint32_t dst, int32_t amt)
{
    t->p[mx_pid(k)] = (int16_t)src, t->p[mx_pid(k) + 1u] = (int16_t)dst, t->p[mx_pid(k) + 2u] = (int16_t)amt;
}

/* ---- DLY: a short note on part ti at velocity vel; its delay tail's energy (the dry note is over before the
 * first echo: 1/8 at 120 BPM, 250 ms) */
static double dly_tail(uint32_t ti, uint32_t vel)
{
    track_t *t = &trk[ti];
    quiet();
    trk_note_on(t, 60, vel);
    run(240);                                           /* (174 ms) */
    trk_note_off(t, 60);
    run(30);                                            /* (the release) */
    run(1500);
    return esum;
}
static void t_dly(void)
{
    char w[200];
    double b20, b127, s20, s127, g20, g127, b2, a2;
    uint32_t i;
    for (i = 0; i < 2u; i++) {
        plain(&trk[i]);
        trk[i].p[P_DLY] = 20;
    }
    trk[1].p[P_LYMODE] = LY_MULTI;                      /* (part 2 heard, on its own) */
    song.g[G_DFDBK] = 30;
    b20 = dly_tail(0, 20), b127 = dly_tail(0, 127), b2 = dly_tail(1, 127);   /* no slot */
    slot(&trk[0], 0, MS_VEL, MT_DLY, 63);
    s20 = dly_tail(0, 20), s127 = dly_tail(0, 127), a2 = dly_tail(1, 127);
    g20 = sqrt(s20 / (b20 + 1e-9)), g127 = sqrt(s127 / (b127 + 1e-9));   /* the return's gain from the slot */
    snprintf(w, sizeof w, "VEL -> DLY +63: the delay return x %.2f at velocity 127, more than 2 x velocity 20's x %.2f",
             g127, g20);
    ok(g127 > 2.0 * g20 && g20 > 0.9, w);
    snprintf(w, sizeof w, "part 2's send unchanged by part 1's slot (tail %.4g, before %.4g)", a2, b2);
    ok(b2 > 0 && fabs(a2 - b2) <= 0.01 * b2, w);
    plain(&trk[0]);                                     /* REV and CHO: the same sums (+V: 32767 x 63 / 64 >> 8) */
    slot(&trk[0], 0, MS_DC, MT_REV, 63);
    slot(&trk[0], 1, MS_DC, MT_CHO, -64);
    trk_note_on(&trk[0], 60, 100);
    run(10);
    snprintf(w, sizeof w, "+V -> REV +63, CHO -64: the part's REV %+d, CHO %+d (125, -127), DLY %+d", trk[0].revmod,
             trk[0].chomod, trk[0].dlymod);
    ok(trk[0].revmod == 125 && trk[0].chomod == -127 && trk[0].dlymod == 0 && trk[1].revmod == 0, w);
    trk_note_off(&trk[0], 60);
    quiet();
    song.g[G_DFDBK] = GP[G_DFDBK].def;
    trk[1].p[P_LYMODE] = LY_OFF;
    plain(&trk[0]);
    plain(&trk[1]);
}

/* ---- ARP: the arp's steps in nb blocks (part 1 only runs one) */
static uint32_t arp_steps(int32_t amt, uint32_t nb)
{
    track_t *t = &trk[0];
    uint32_t b, n = 0, last;
    quiet();
    plain(t);
    t->p[P_AMODE] = 1, t->p[P_ARATE] = 1, t->p[P_AGATE] = 64, t->p[P_APROB] = 127, t->p[P_AOCT] = 1;   /* (1: 1/8) */
    if (amt)
        slot(t, 0, MS_DC, MT_ARP, amt);
    t->arp_idx = 0;
    input_on(t, 60, 100);
    input_on(t, 64, 100);
    input_on(t, 67, 100);
    run(700);                                           /* (the first steps: the rate settles) */
    last = t->arp_idx;
    for (b = 0; b < nb; b++) {
        run(1);
        if (t->arp_idx != last)
            n++, last = t->arp_idx;
    }
    input_off(t, 60);
    input_off(t, 64);
    input_off(t, 67);
    t->p[P_AMODE] = 0;
    run(50);
    return n;
}
static void t_arp(void)
{
    char w[200];
    uint32_t nb = 2u * FS / CTL, base = arp_steps(0, nb), fast = arp_steps(63, nb), slow = arp_steps(-63, nb);
    snprintf(w, sizeof w, "the arp at 1/8: %u steps in 2 s (120 BPM: 8)", base);
    ok(base >= 7u && base <= 9u, w);
    snprintf(w, sizeof w, "+V -> ARP +63: 1/32, %u steps (4 x 1/8's)", fast);
    ok(fast >= 30u && fast <= 34u, w);
    snprintf(w, sizeof w, "+V -> ARP -63: 1/4, %u steps", slow);
    ok(slow >= 3u && slow <= 5u, w);
    plain(&trk[0]);
}

/* ---- LFO1: LFO 1's phase advance over nb blocks with the wheel at mw */
static double l1_cycles(uint32_t mw, uint32_t nb)
{
    track_t *t = &trk[0];
    uint64_t adv = 0;
    uint32_t b, old;
    quiet();
    plain(t);
    t->p[P_LRATE] = 40;
    slot(t, 0, MS_MW, MT_L1RATE, 63);
    part_ctl[ctl_part(t)].mw = (uint8_t)mw;
    trk_note_on(t, 60, 100);
    run(10);
    old = t->lfo_ph;
    for (b = 0; b < nb; b++) {
        run(1);
        adv += (uint32_t)(t->lfo_ph - old);
        old = t->lfo_ph;
    }
    trk_note_off(t, 60);
    part_ctl[ctl_part(t)].mw = 0;
    return (double)adv / 4294967296.0;
}
static void t_l1rate(void)
{
    char w[200];
    uint32_t nb = FS / CTL;
    double c0 = l1_cycles(0, nb), c1 = l1_cycles(127, nb);
    snprintf(w, sizeof w, "MW -> LFO1 +63: LFO 1 at the wheel 127 %.2f cycles a second, at 0 %.2f", c1, c0);
    ok(c1 > 2.0 * c0 && c0 > 0, w);
    plain(&trk[0]);
}

/* ---- E1AMT: the held note's spectral centroid (Hz) */
static double centroid(uint32_t a, uint32_t n)
{
    double num = 0, den = 0;
    uint32_t k, i;
    for (k = 1; k < n / 2u; k++) {
        double re = 0, im = 0, m;
        for (i = 0; i < n; i++) {
            double x = cl[a + i] * (0.5 - 0.5 * cos(2 * M_PI * i / n));
            re += x * cos(2 * M_PI * k * i / n);
            im += x * sin(2 * M_PI * k * i / n);
        }
        m = sqrt(re * re + im * im);
        num += m * k * (double)FS / n, den += m;
    }
    return num / (den + 1e-9);
}
static double e1_centroid(int32_t ed, int32_t amt)
{
    track_t *t = &trk[0];
    double c;
    quiet();
    plain(t);
    t->p[P_E0] = 0, t->p[P_E4] = 40, t->p[P_E5] = 10;
    t->p[P_ED_FLT] = (int16_t)ed;
    if (amt)
        slot(t, 0, MS_DC, MT_E1AMT, amt);
    trk_note_on(t, 48, 100);
    run(300);
    run(64);
    c = centroid(0, 2048);
    trk_note_off(t, 48);
    return c;
}
static void t_e1amt(void)
{
    char w[200];
    double c0 = e1_centroid(0, 0), c60 = e1_centroid(60, 0), cx = e1_centroid(60, -64);
    snprintf(w, sizeof w, "ENV DEST FLT 60 opens the filter: centroid %.0f Hz, FLT 0 %.0f Hz", c60, c0);
    ok(c60 > c0 * 1.2, w);
    snprintf(w, sizeof w, "+V -> E1AMT -64: ENV 1 no longer opens it, %.0f Hz (within 5 %% of %.0f)", cx, c0);
    ok(fabs(cx - c0) < 0.05 * c0, w);
    plain(&trk[0]);
}

/* ---- 24 SLOTS: twelve cables in slots 13..24 play exactly as in slots 1..12; twelve more there are heard; all 24
 * live at once are all read (voice.c mx_load) */
static const int8_t S24[12][3] = {{MS_ENV2, MT_CUT, 40}, {MS_ENV2, MT_SHAPE, 30}, {MS_DC, MT_PITCH, 3}, {MS_VEL, MT_AMP, -20},
                                  {MS_VEL, MT_PAN, 50}, {MS_KEY, MT_CUT, 10}, {MS_KEY, MT_E1 + 1, 25}, {MS_GATE, MT_SHAPE, -15},
                                  {MS_DC, MT_RES2, 20}, {MS_ENV1, MT_CUT, -30}, {MS_ENV1, MT_PITCH, 1}, {MS_DC, MT_PAN, -40}};   /* (engine-dests: RING lives on MARE alone) */
                                  /* (sources that play the same each note: no LFO phase, noise or random carried over) */
static uint32_t s24_hash;
static void s24_note(void)
{
    uint32_t i;
    quiet();
    trk_note_on(&trk[0], 57, 100);
    run(300);
    trk_note_off(&trk[0], 57);
    run(100);
    s24_hash = 2166136261u;
    for (i = 0; i < ncap; i++)
        s24_hash = (s24_hash ^ (uint32_t)cl[i]) * 16777619u;
}
static void t_slots24(void)
{
    track_t *t = &trk[0];
    uint32_t k, ha, hb, hc;
    double ea, ec;
    mx_t x;
    char w[160];
    ok(MX_SLOTS == 24u && mx_pid(12) == P_M13SRC && mx_pid(23) == P_M24SRC && P_M24AMT + 2 == P_JRNG && P_MW34 + 1 == P_JRNG && P_JCHR + 3 == P_E0 && P_OKYB + 2 == P_E0 && P_OPW + 1 == P_E0,
       "24 slots: 13..24 at P_M13SRC .. P_M24AMT, then WAV34, (Stallion) PONY's EDIT 3, RODEO's KYBD PW0 just before P_E0");
    plain(t);
    for (k = 0; k < 12u; k++)
        slot(t, k, S24[k][0], S24[k][1], S24[k][2]);
    s24_note();
    ha = s24_hash, ea = esum;
    plain(t);
    for (k = 0; k < 12u; k++)
        slot(t, 12u + k, S24[k][0], S24[k][1], S24[k][2]);
    s24_note();
    hb = s24_hash;
    ok(ha == hb, "the same twelve cables in slots 13..24 render bit for bit as in slots 1..12");
    for (k = 0; k < 12u; k++)
        slot(t, k, S24[k][0], S24[k][1], S24[k][2]);
    for (k = 0; k < 12u; k++)
        slot(t, 12u + k, MS_DC, MT_PITCH, 2);          /* (twelve more: +V to the pitch) */
    mx_load(t->p, &x, mx_dst_live(t->engine));   /* (engine-dests) as the voice loads it */
    snprintf(w, sizeof w, "all 24 live: mx_load reads %u", x.n);
    ok(x.n == 24u && (x.dsts >> MT_PAN & 1u) && (x.srcs >> MS_GATE & 1u), w);
    s24_note();
    hc = s24_hash, ec = esum;
    snprintf(w, sizeof w, "the 13th..24th cables are heard (energy %.3g vs %.3g)", ec, ea);
    ok(hc != ha, w);
    plain(t);
}

int main(void)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NPART; i++)
        host_preset(&trk[i], 0, 0);
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;
    t_dly();
    t_arp();
    t_l1rate();
    t_e1amt();
    t_slots24();
    printf(fails ? "mxdest: %d FAILED\n" : "mxdest: all ok\n", fails);
    return fails != 0;
}
