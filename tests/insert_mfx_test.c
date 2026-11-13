/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 John Gorman (Clipper) */
/* Clipper 1.1 (CLIPPER_SYNTH 1): the INSERT's types after the PALOMINO-08's effects, measured.
 *   PITCH    a sine shifted by +12, -12, +7 semitones and +50 cents lands on its new frequency (Goertzel), the old
 *            one gone; FDBK rings on after the input stops, then dies away
 *   PIT2     both voices at their pitches; BAL 0 the first alone
 *   PHAS6    still (DEPTH 0): three notches where PHASR has two (the six stages against four)
 *   DELAY    an impulse's echo at TIME, the next at twice TIME, lower by about FDBK; FDBK 0 one echo; on a sound the
 *            part runs on while the echoes ring (past the INSERT's 47 ms tail), then stops
 *   LOCMP    COMP (64: the threshold between them) narrows an 18 dB step between two levels, COMP 0 leaves it; LOFI 127 a lower resolution
 *   presets  LOCMP (TYPE 16) back from a user preset; a record without the tail (SLOOP's save) reads TYPE OFF */
#define main hostsim_main
#include <stddef.h>
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static const uint8_t TRK_DEF[NPART][2] = {{ENGI_HYBRID, 0}, {1, 0}, {4, 5}};   /* (ui.c, Clipper) */
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? TRK_DEF[i][0] : 0u; }
#include "../firmware/src/project.c"
#define UP_HOST 1
#include "../firmware/src/upreset.c"

static int fails;
static void ok(int c, const char *what)
{
    printf("insert 1.1: %-90s %s\n", what, c ? "ok" : "FAIL");
    fails += !c;
}

#define NS (CTL * 1024u)
static int32_t xin[NS], yout[NS];
/* the INSERT of type ty (A B C, MIX 127) over xin from rest, into yout */
static void through(int ty, int a, int b, int c)
{
    static ins_t s;
    static int16_t p[P_COUNT];
    static int32_t blk[CTL];
    uint32_t k, i;
    memset(&s, 0, sizeof s);
    memset(p, 0, sizeof p);
    p[P_ITYPE] = (int16_t)ty, p[P_IA] = (int16_t)a, p[P_IB] = (int16_t)b, p[P_IC] = (int16_t)c, p[P_IMIX] = 127;
    for (k = 0; k < NS / CTL; k++) {
        for (i = 0; i < CTL; i++)
            blk[i] = xin[k * CTL + i];
        ins_run(&s, p, blk, CTL, 1);
        for (i = 0; i < CTL; i++)
            yout[k * CTL + i] = blk[i];
    }
}
static void sines(double f1, double a1, double f2, double a2)
{
    uint32_t i;
    for (i = 0; i < NS; i++)
        xin[i] = (int32_t)(a1 * sin(2 * M_PI * f1 * i / 44100.0) + a2 * sin(2 * M_PI * f2 * i / 44100.0));
}
/* the amplitude of frequency f in yout[from .. from + n) (Goertzel, a Hann window) */
static double tone(double f, uint32_t from, uint32_t n)
{
    double w = 2 * M_PI * f / 44100.0, c = 2 * cos(w), s0, s1 = 0, s2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double h = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1));
        s0 = yout[from + i] * h + c * s1 - s2;
        s2 = s1, s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) * 4.0 / n;
}
/* the strongest line within 30 Hz of f (a two-head shifter puts a sine's energy in lines the grain rate apart, the
 * strongest a few Hz from the exact ratio: the phase each head jumps by at its wrap) */
static double band(double f, uint32_t from, uint32_t n)
{
    double m = 0, g, d;
    for (d = -30; d <= 30; d += 1)
        if ((g = tone(f + d, from, n)) > m)
            m = g;
    return m;
}
static double rms_of(const int32_t *x, uint32_t from, uint32_t n)
{
    double e = 0;
    uint32_t i;
    for (i = from; i < from + n; i++)
        e += (double)x[i] * x[i];
    return sqrt(e / n);
}

static void t_pitch(void)
{
    static const struct { int a, b; double ratio; const char *what; } C[] = {
        {95, 64, 2.0, "+12"}, {32, 64, 0.5, "-12"}, {81, 64, 1.4983, "+7"}, {64, 127, 1.0293, "0 st +50 ct"},
    };
    char w[160];
    uint32_t k;
    for (k = 0; k < 4; k++) {
        double t, o;
        sines(440, 16000, 0, 0);
        through(IT_PITCH, C[k].a, C[k].b, 0);
        t = band(440 * C[k].ratio, 8192, 16384), o = k == 3 ? tone(440, 8192, 16384) : band(440, 8192, 16384);
        snprintf(w, sizeof w, "PITCH %s: 440 Hz -> %.0f Hz (%.0f there, %.0f left at 440)", C[k].what, 440 * C[k].ratio, t, o);
        ok(t > 6000 && o < t / 10, w);
    }
    {   /* FDBK: rings on after the input stops, then dies away */
        uint32_t i;
        double r0, rmid, rend;
        sines(440, 16000, 0, 0);
        for (i = NS / 4; i < NS; i++)
            xin[i] = 0;
        through(IT_PITCH, 95, 64, 127);
        r0 = rms_of(yout, NS / 4 + 2048, 2048), rmid = rms_of(yout, NS / 4 + 8192, 2048), rend = rms_of(yout, NS - 4096, 4096);
        sines(440, 16000, 0, 0);
        for (i = NS / 4; i < NS; i++)
            xin[i] = 0;
        through(IT_PITCH, 95, 64, 0);
        snprintf(w, sizeof w, "PITCH FDBK 127: rings after the input (%.0f, then %.0f; without FDBK %.0f), dies away (%.1f)",
                 r0, rmid, rms_of(yout, NS / 4 + 2048, 2048), rend);
        ok(r0 > 300 && rms_of(yout, NS / 4 + 2048, 2048) < 1 && rend < 1, w);
    }
}

static void t_pitch2(void)
{
    double lo, hi, mid;
    char w[160];
    sines(440, 16000, 0, 0);
    through(IT_PITCH2, 32, 81, 64);                     /* -12 and +7, balanced */
    lo = band(220, 8192, 16384), hi = band(440 * 1.4983, 8192, 16384), mid = band(440, 8192, 16384);
    snprintf(w, sizeof w, "PIT2 -12 / +7: both voices (%.0f at 220 Hz, %.0f at 659 Hz), 440 Hz gone (%.0f)", lo, hi, mid);
    ok(lo > 2500 && hi > 2500 && lo < 2 * hi && hi < 2 * lo && mid < lo / 10, w);
    through(IT_PITCH2, 32, 81, 0);
    lo = band(220, 8192, 16384), hi = band(440 * 1.4983, 8192, 16384);
    snprintf(w, sizeof w, "PIT2 BAL 0: the first voice alone (%.0f at 220 Hz, %.0f at 659 Hz)", lo, hi);
    ok(lo > 6000 && hi < lo / 20, w);
}

/* notches of a still swept type over 60 Hz .. 16 kHz: local minima under a fifth of the loudest */
static int notches(int ty)
{
    double g[96], mx = 0;
    int k, n = 0;
    for (k = 0; k < 96; k++) {
        double f = 60 * pow(16000.0 / 60, k / 95.0);
        sines(f, 12000, 0, 0);
        through(ty, 0, 0, 0);                           /* RATE 0, DEPTH 0, FDBK 0 */
        g[k] = tone(f, 8192, 16384) / 12000;
        mx = g[k] > mx ? g[k] : mx;
    }
    for (k = 1; k < 95; k++)
        n += g[k] < g[k - 1] && g[k] <= g[k + 1] && g[k] < mx / 5;
    return n;
}
static void t_phase6(void)
{
    char w[120];
    int n6 = notches(IT_PHASER6), n4 = notches(IT_PHASER);
    snprintf(w, sizeof w, "PHAS6 still: %d notches (PHASR %d)", n6, n4);
    ok(n6 == 3 && n4 == 2, w);
}

/* the sample of yout's largest |value| in [from, to) */
static uint32_t peak_at(uint32_t from, uint32_t to, double *v)
{
    uint32_t i, at = from;
    *v = 0;
    for (i = from; i < to; i++)
        if (fabs((double)yout[i]) > *v)
            *v = fabs((double)yout[i]), at = i;
    return at;
}
static void t_delay(void)
{
    const uint32_t t0 = 8192u, d = 882u;                /* TIME 52: DI_MS[13] = 20 ms */
    uint32_t a1, a2;
    double v0, v1, v2, v3;
    char w[200];
    memset(xin, 0, sizeof xin);
    xin[t0] = xin[t0 + 1] = 16000;                      /* (two samples: the line keeps pairs) */
    through(IT_DELAY, 52, 64, 127);
    peak_at(t0, t0 + 4, &v0);
    a1 = peak_at(t0 + d / 2, t0 + d + d / 2, &v1);
    a2 = peak_at(t0 + d + d / 2, t0 + 2 * d + d / 2, &v2);
    snprintf(w, sizeof w, "DELAY 20 ms, FDBK 64: echoes at +%u and +%u samples (882, 1764), %.2f and %.2f of the dry, "
             "the second %.2f of the first", a1 - t0, a2 - t0, v1 / v0, v2 / v0, v2 / v1);
    ok(abs((int)(a1 - t0) - (int)d) <= 9 && abs((int)(a2 - t0) - 2 * (int)d) <= 9 &&   /* (0.2 ms: the pairs, TONE) */ v1 > 0.3 * v0 && v2 / v1 > 0.3 &&
       v2 / v1 < 0.6, w);
    through(IT_DELAY, 52, 0, 127);
    peak_at(t0 + d / 2, t0 + d + d / 2, &v1);
    peak_at(t0 + d + d / 2, t0 + 2 * d + d / 2, &v2);
    snprintf(w, sizeof w, "DELAY FDBK 0: one echo (%.0f), none after it (%.1f)", v1, v2);
    ok(v1 > 3000 && v2 < 2, w);
    through(IT_DELAY, 52, 64, 0);                       /* TONE 0: the echoes darkened */
    peak_at(t0 + d / 2, t0 + d + d / 2, &v3);
    snprintf(w, sizeof w, "DELAY TONE 0: the echo's peak lower, smeared (%.0f, bright %.0f)", v3, v1);
    ok(v3 < 0.5 * v1, w);
}

/* the delay's echoes on a sound: the part runs on past the INSERT's tail while they ring, then stops */
#define NCAP (4096u * 8u)
static int32_t cl[NCAP];
static uint32_t ncap;
static void run(uint32_t nb)
{
    static int32_t o[CTL * 2];
    uint32_t b, i;
    ncap = 0;
    for (b = 0; b < nb; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            if (ncap < NCAP)
                cl[ncap++] = o[2 * i];
    }
}
static void quiet(void)
{
    uint32_t i, k;
    for (i = 0; i < NPART; i++)
        trk_all_off(&trk[i]);
    run(400);
    for (i = 0; i < NPART; i++)
        for (k = 0; k < NVOICE; k++)
            trk[i].v[k].active = trk[i].v[k].gate = 0, trk[i].v[k].stage = 0, trk[i].v[k].env = trk[i].v[k].env_out = 0;
    run(300);
}
static void plain(track_t *t)
{
    host_preset(t, 0u, 0);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    t->p[P_LD_PIT] = t->p[P_LD_FLT] = t->p[P_LD_SHP] = t->p[P_LD_AMP] = t->p[P_ED_PIT] = 0;
    t->p[P_ATK] = 0, t->p[P_SUS] = 127, t->p[P_REL] = 0, t->p[P_VOICE] = V_POLY, t->p[P_PAN] = 0;
    t->p[P_MWDST] = t->p[P_ATDST] = MD_OFF;
    t->p[P_ITYPE] = 0, t->p[P_IMIX] = 127;
}
static void t_delay_tail(void)
{
    track_t *t = &trk[0];
    uint32_t b, quiet_at = 0, ring_late = 0, still = 0;
    char w[200];
    quiet();
    plain(t);
    t->p[P_ITYPE] = IT_DELAY, t->p[P_IA] = 127, t->p[P_IB] = 110, t->p[P_IC] = 110;   /* 92 ms, FDBK 0.79 */
    trk_note_on(t, 57, 100);
    run(60);
    trk_note_off(t, 57);
    for (b = 0; b < 30000u && !quiet_at; b++) {
        uint32_t i, e = 0;
        run(1);
        for (i = 0; i < CTL; i++)
            e |= (uint32_t)(cl[i] != 0);
        if (b > 600u && b < 700u && e)
            ring_late = 1;                              /* (440 .. 510 ms after: the fifth echo or so) */
        still = e ? 0u : still + 1u;
        if (b > 700u && still >= 400u)
            quiet_at = b - 400u;                        /* (silent for good: 0.3 s of exact 0) */
    }
    snprintf(w, sizeof w, "DELAY on a sound: the echoes ring past the INSERT's tail (still at ~0.5 s: %s), then the part "
             "stops (block %u)", ring_late ? "yes" : "no", quiet_at);
    ok(ring_late && quiet_at > 700u && quiet_at < 30000u, w);
    t->p[P_ITYPE] = 0;
    quiet();
}

static void t_locomp(void)
{
    uint32_t i;
    double hi, lo, hi0, lo0, err;
    char w[200];
    static int32_t clean[NS];
    for (i = 0; i < NS; i++)                            /* loud, then 18 dB lower */
        xin[i] = (int32_t)((i < NS / 2 ? 24000 : 3020) * sin(2 * M_PI * 220 * i / 44100.0));
    through(IT_LOCOMP, 0, 0, 127);
    hi0 = rms_of(yout, NS / 2 - 8192, 8192), lo0 = rms_of(yout, NS - 8192, 8192);
    through(IT_LOCOMP, 64, 0, 127);                     /* (the threshold -24 dB: between the two) */
    hi = rms_of(yout, NS / 2 - 8192, 8192), lo = rms_of(yout, NS - 8192, 8192);
    memcpy(clean, yout, sizeof clean);
    snprintf(w, sizeof w, "LOCMP: COMP 64 narrows an 18 dB step to %.1f dB; COMP 0 leaves %.1f dB",
             20 * log10(hi / lo), 20 * log10(hi0 / lo0));
    ok(20 * log10(hi / lo) < 12 && fabs(20 * log10(hi0 / lo0) - 18) < 1, w);
    through(IT_LOCOMP, 64, 127, 127);
    for (err = 0, i = NS / 2 - 8192; i < NS / 2; i++)
        err += (double)(yout[i] - clean[i]) * (yout[i] - clean[i]);
    err = sqrt(err / 8192);
    snprintf(w, sizeof w, "LOCMP LOFI 127: the lo-fi's error %.0f (%.1f dB under the sound)", err, 20 * log10(hi / err));
    ok(20 * log10(hi / err) < 30, w);
}

static void t_presets(void)
{
    up_rec_t r;
    int16_t v[P_COUNT], out[P_COUNT], def[P_COUNT];
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        v[i] = def[i] = i >= P_E0 ? 0 : TP[i].def;
    v[P_ITYPE] = IT_LOCOMP, v[P_IA] = 101, v[P_IMIX] = 99;
    memset(&r, 0, sizeof r);
    r.used = UP_USED, r.name[0] = 'X';
    up_rec_set(&r, v);
    up_params(&r, out, def);
    ok(out[P_ITYPE] == IT_LOCOMP && out[P_IA] == 101 && out[P_IMIX] == 99 && TP[P_ITYPE].max == IT_N - 1,
       "user presets: LOCMP (TYPE 16) back exactly");
    v[P_ITYPE] = IT_DELAY;
    up_rec_set(&r, v);
    up_params(&r, out, def);
    ok(out[P_ITYPE] == IT_DELAY, "user presets: DELAY (TYPE 15, four bits) back exactly");
}

int main(void)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NPART; i++)
        host_preset(&trk[i], i ? 1u : ENGI_HYBRID, 0);
    clipper_proj_neutral();
    trk[1].p[P_LYMODE] = LY_OFF;
    song.g[G_BPM] = 120;
    t_pitch();
    t_pitch2();
    t_phase6();
    t_delay();
    t_delay_tail();
    t_locomp();
    t_presets();
    if (fails)
        printf("insert 1.1: %d FAILED\n", fails);
    return fails != 0;
}
