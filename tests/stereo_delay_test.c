/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Eberon Limited (John Gorman) */
/* the INSERT's stereo analog delay, STDLY and PPDLY (fx.c ins_sdly), measured on its own (the part's
 * mid and side in, as mix_part1 hands them over):
 *   times     an impulse's echoes at TIME and twice TIME, on MARE (its arena's lines) and another engine (the INSERT's
 *             line); SPACE puts line 2 at x 0.71 .. x 1.41 of line 1
 *   tone      the first echo of a noise burst: its -3 dB bandwidth and centroid fall as TIME rises (a BBD clocked at
 *             stages / TIME); printed per TIME
 *   riff      the original's delay demo: a note of its colour, its echo darker by TIME; the two-note riff synced to
 *             TIME 428 ms builds +7 dB over 3 s as the original's at some FDBK (printed per FDBK, with the loop gain)
 *   stable    FDBK 127 on a burst far past full scale: it sings on, bounded; a FDBK 64 tail dies to exact 0 (the part
 *             stops: ins_ringing); FDBK 0 one echo
 *   balance   a centred input (left = right): left and right within 1 dB over the whole tail, STDLY and PPDLY, every
 *             SPACE and FDBK (the factory presets' rule); a one-sided input crosses in PPDLY, stays in STDLY
 *   glide     TIME and SPACE turned while a sine plays through: no click (the output's second difference stays within
 *             the steady state's bound), at the short end too
 *   MARE      its presets through the whole mix: a mono one exactly centred, a stereo one's echo as balanced as it */
#define main hostsim_main
#include <stddef.h>
#include "hostsim.c"
#undef main

static int fails;
static void ok(int c, const char *what)
{
    printf("stereo delay: %-100s %s\n", what, c ? "ok" : "FAIL");
    fails += !c;
}

#define FS 44100.0
#define NS (CTL * 8192u)                               /* 5.9 s */
static int32_t inl[NS], inr[NS], outl[NS], outr[NS];
static int16_t pp[P_COUNT];

/* the INSERT of type ty on engine eng over inl / inr from rest (MIX mix), into outl / outr; at block chg the values
 * a2 / c2 take over (a turn of TIME / SPACE) */
static void through2(uint32_t eng, int ty, int a, int b, int c, int mix, uint32_t ns, uint32_t chg, int a2, int c2)
{
    static int32_t blk[CTL];
    track_t *t = &trk[0];
    ins_t *s = &ins[0];
    uint32_t k, i;
    t->engine = (uint8_t)eng;
    eng_arena_own[0] = 0;                               /* (the arena claimed afresh) */
    memset(s, 0, sizeof *s);
    memset(pp, 0, sizeof pp);
    pp[P_ITYPE] = (int16_t)ty, pp[P_IA] = (int16_t)a, pp[P_IB] = (int16_t)b, pp[P_IC] = (int16_t)c;
    pp[P_IMIX] = (int16_t)mix;
    for (k = 0; k < ns / CTL; k++) {
        if (k == chg)
            pp[P_IA] = (int16_t)a2, pp[P_IC] = (int16_t)c2;
        part_side_on = 1;
        for (i = 0; i < CTL; i++) {
            int32_t l = inl[k * CTL + i], r = inr[k * CTL + i];
            blk[i] = (l + r) >> 1, part_side[i] = (l - r) >> 1;
        }
        ins_run(s, pp, blk, CTL, 1);
        for (i = 0; i < CTL; i++)
            outl[k * CTL + i] = blk[i] + part_side[i], outr[k * CTL + i] = blk[i] - part_side[i];
    }
}

static uint32_t sd_rng = 0x2545F491u;
static double white(void)
{
    sd_rng = sd_rng * 1664525u + 1013904223u;
    return ((int32_t)(sd_rng >> 8) - (1 << 23)) / (double)(1 << 23);
}
static void silence(void)
{
    memset(inl, 0, sizeof inl);
    memset(inr, 0, sizeof inr);
}
/* a noise burst of len samples at t0, amplitude amp, both sides the same */
static void burst(uint32_t t0, uint32_t len, double amp)
{
    uint32_t i;
    silence();
    sd_rng = 0x2545F491u;
    for (i = 0; i < len; i++)
        inl[t0 + i] = inr[t0 + i] = (int32_t)(amp * white());
}

/* the spectrum (power, nb bins over 0 .. FS / 2) of x[from .. from + n), n a power of 2, a Hann window */
#define FN 8192u
static double fre[FN], fim[FN];
static void fft(double *re, double *im, uint32_t n)
{
    uint32_t i, j = 0, len, k;
    for (i = 1; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double tr = re[i], ti = im[i];
            re[i] = re[j], im[i] = im[j], re[j] = tr, im[j] = ti;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double a = -2 * M_PI / len, wr = cos(a), wi = sin(a);
        for (i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (k = 0; k < len / 2; k++) {
                double ur = re[i + k], ui = im[i + k];
                double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                double t = cr * wr - ci * wi;
                re[i + k] = ur + vr, im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr, im[i + k + len / 2] = ui - vi;
                ci = cr * wi + ci * wr, cr = t;
            }
        }
    }
}
static void spectrum(const int32_t *x, uint32_t from, double *pw)
{
    uint32_t i;
    for (i = 0; i < FN; i++)
        fre[i] = x[from + i] * (0.5 - 0.5 * cos(2 * M_PI * i / (FN - 1))), fim[i] = 0;
    fft(fre, fim, FN);
    for (i = 0; i < FN / 2; i++)
        pw[i] = fre[i] * fre[i] + fim[i] * fim[i];
}
static double centroid(const double *pw, double fmax)
{
    double s = 0, sw = 0;
    uint32_t i;
    for (i = 1; i < FN / 2 && i * FS / FN < fmax; i++)
        s += pw[i], sw += pw[i] * i * FS / FN;
    return s > 0 ? sw / s : 0;
}
static double energy(const int32_t *x, uint32_t from, uint32_t n)
{
    double e = 0;
    uint32_t i;
    for (i = from; i < from + n && i < NS; i++)
        e += (double)x[i] * x[i];
    return e;
}
static double db(double e1, double e2) { return 10 * log10((e1 + 1e-9) / (e2 + 1e-9)); }

/* the envelope's peak (abs, 64-sample mean) within [from, to) */
static uint32_t env_peak(const int32_t *x, uint32_t from, uint32_t to, double *v)
{
    uint32_t i, at = from;
    double m = 0, acc = 0;
    *v = 0;
    for (i = from; i < to; i++) {
        acc += fabs((double)x[i]) - (i >= from + 64 ? fabs((double)x[i - 64]) : 0);
        if (i >= from + 64 && acc > m)
            m = acc, at = i - 32;
    }
    *v = m / 64;
    return at;
}

static const int SD_MS_T[32] = {35, 39, 43, 47, 52, 58, 64, 71, 78, 86, 95, 105, 116, 129, 142, 157, 174, 192, 212, 235,
                                259, 287, 317, 350, 387, 428, 473, 523, 578, 638, 706, 780};
static int knob_of(int ms)                              /* the TIME knob value whose part is ms */
{
    int a;
    for (a = 0; a < 128; a++)
        if (SD_MS_T[(a * 32) >> 7] == ms)
            return a;
    return 127;
}

/* ---- times: an impulse (a 1 ms burst) at 0.1 s, FDBK 64; the echoes on the left at T and 2T, the right at T x SPACE */
static void t_times(void)
{
    static const struct { uint32_t eng; int ms; } C[] = {{ENGI_MARE, 35}, {ENGI_MARE, 428}, {ENGI_MARE, 780},
                                                         {ENGI_HYBRID, 35}, {ENGI_HYBRID, 212}, {ENGI_HYBRID, 387}};
    uint32_t j, t0 = 4410u;
    char w[240];
    for (j = 0; j < NELEM(C); j++) {
        double d = C[j].ms * FS / 1000, v1, v2, v0;
        uint32_t a1, a2, lo = (uint32_t)(d * 0.8), hi = (uint32_t)(d * 1.2);
        burst(t0, 44, 20000);
        through2(C[j].eng, IT_SDLY, knob_of(C[j].ms), 64, 64, 127, t0 + (uint32_t)(2.5 * d) + 4096u, ~0u, 0, 0);
        env_peak(inl, t0, t0 + 200, &v0);
        a1 = env_peak(outl, t0 + lo, t0 + hi, &v1);
        a2 = env_peak(outl, t0 + 2 * lo, t0 + 2 * hi, &v2);
        snprintf(w, sizeof w, "%s TIME %d ms: echoes at %.1f and %.1f ms (%.2f, %.2f of the burst)",
                 C[j].eng == ENGI_MARE ? "MARE " : "other", C[j].ms, (a1 - t0) * 1000 / FS, (a2 - t0) * 1000 / FS,
                 v1 / v0, v2 / v0);
        ok(fabs((a1 - t0) - d) < 0.03 * d + 40 && fabs((a2 - t0) - 2 * d) < 0.03 * d + 80 && v1 > 0.1 * v0 && v2 < v1, w);
    }
    {   /* SPACE +8 (127) and -8 (0): the right at x 1.41 / x 0.71 */
        static const int SC[2] = {127, 0};
        for (j = 0; j < 2; j++) {
            double d = 212 * FS / 1000, r = SC[j] ? 1.4142 : 0.7071, vl, vr;
            uint32_t al, ar;
            burst(t0, 44, 20000);
            through2(ENGI_MARE, IT_SDLY, knob_of(212), 0, SC[j], 127, t0 + (uint32_t)(2 * d) + 4096u, ~0u, 0, 0);
            al = env_peak(outl, t0 + (uint32_t)(0.5 * d), t0 + (uint32_t)(1.6 * d), &vl);
            ar = env_peak(outr, t0 + (uint32_t)(0.5 * d), t0 + (uint32_t)(1.6 * d), &vr);
            snprintf(w, sizeof w, "SPACE %+d: left echo %.1f ms, right %.1f ms (x %.3f, %.3f asked)", SC[j] ? 8 : -8,
                     (al - t0) * 1000 / FS, (ar - t0) * 1000 / FS, (double)(ar - t0) / (al - t0), r);
            ok(fabs((double)(ar - t0) / (al - t0) - r) < 0.03, w);
        }
    }
}

/* ---- tone: the first echo of a 50 ms noise burst against the burst: -3 dB bandwidth and centroid per TIME */
static double bw3(const double *e, const double *x)
{   /* the frequency where echo / dry first falls 3 dB under its level at 100..200 Hz (third-octave smoothing) */
    double ref = 0, f;
    uint32_t i, n = 0;
    for (i = 1; i < FN / 2; i++)
        if (i * FS / FN >= 100 && i * FS / FN < 200)
            ref += e[i] / (x[i] + 1e-9), n++;
    ref /= n;
    for (f = 200; f < 20000; f *= 1.02) {
        double s = 0, sx = 0;
        uint32_t lo = (uint32_t)(f * 0.89 * FN / FS), hi = (uint32_t)(f * 1.12 * FN / FS) + 1;
        for (i = lo; i < hi && i < FN / 2; i++)
            s += e[i], sx += x[i];
        if (s / (sx + 1e-9) < ref * 0.5)
            return f;
    }
    return 20000;
}
static double PWD[FN / 2], PWE[FN / 2];
static void t_tone(void)
{
    static const int MS[] = {35, 71, 142, 287, 428, 780};
    static const uint32_t ENG[2] = {ENGI_MARE, ENGI_HYBRID};
    uint32_t j, g, t0 = 4410u, len = 2205u;
    char w[240];
    for (g = 0; g < 2; g++) {
        double lastb = 1e9, lastc = 1e9;
        int mono = 1;
        for (j = 0; j < NELEM(MS); j++) {
            double d = MS[j] * FS / 1000, b, c, c0;
            uint32_t at;
            burst(t0, len, 12000);
            through2(ENG[g], IT_SDLY, knob_of(MS[j]), 0, 64, 127, t0 + (uint32_t)d + len + FN + 64, ~0u, 0, 0);
            at = t0 + (uint32_t)d - (FN - len) / 2;     /* the echo in the window's middle */
            spectrum(inl, t0 - (FN - len) / 2, PWD);
            spectrum(outl, at, PWE);
            b = bw3(PWE, PWD), c = centroid(PWE, 20000), c0 = centroid(PWD, 20000);
            printf("stereo delay:   %s TIME %3d ms: echo -3 dB at %5.0f Hz, centroid %5.0f Hz (the burst's %5.0f)\n",
                   g ? "other" : "MARE ", MS[j], b, c, c0);
            mono &= b <= lastb * 1.02 && c <= lastc * 1.02;
            lastb = b, lastc = c;
        }
        snprintf(w, sizeof w, "%s: the echo darker as TIME rises (bandwidth and centroid fall)", g ? "other" : "MARE ");
        ok(mono, w);
    }
}

/* ---- riff: the original's delay demo (a two-note riff on D#2, each note gated 200 ms, through a delay at the riff's
 * period). A note of the riff's colour (four saws stacked in octaves and a fifth through two poles at 1.5 kHz: its
 * centroid ~520 Hz, the original's 513-552 Hz) alone: its first echo's centroid against its own, per TIME. Then the riff at the delay's period (428 ms,
 * synced as the original's 419 ms is to its riff), MIX 64: its level from the first riff to the one after 2.5 s, per
 * FDBK, and the loop gain (the second echo against the first) */
static void note_in(uint32_t t0, uint32_t len, double f, double amp)
{
    uint32_t i;
    double l1 = 0, l2 = 0, k = 1 - exp(-2 * M_PI * 5000 / FS);
    for (i = 0; i < len; i++) {
        double g = fmin(1.0, i / 441.0) * fmin(1.0, (len - i) / 441.0), x = 0;
        uint32_t m;
        for (m = 1; m <= 4; m++)                        /* (four VCOs: the note, its octave, the fifth above, two octaves) */
            x += (2 * fmod(m * f * i / FS, 1.0) - 1) / 4;
        l1 += (x - l1) * k, l2 += (l1 - l2) * k;
        inl[t0 + i] += (int32_t)(amp * l2 * g), inr[t0 + i] += (int32_t)(amp * l2 * g);
    }
}
static void t_riff(void)
{   /* (the response at 428 ms, sines: -0.4 dB at 250 Hz, -1.3 at 500 Hz, -4.9 at 1 kHz, -16 at 2 kHz) */
    static const int MS[] = {105, 212, 428, 780}, FB[] = {50, 60, 70, 80, 90, 100};
    uint32_t j, t0 = 4410u, len = 8820u, per;
    char w[260];
    double c428 = 0, cdry = 0;
    for (j = 0; j < NELEM(MS); j++) {
        double d = MS[j] * FS / 1000, cd, ce;
        silence();
        note_in(t0, len, 77.78, 16000);
        through2(ENGI_MARE, IT_SDLY, knob_of(MS[j]), 0, 64, 127, t0 + (uint32_t)d + len + 64, ~0u, 0, 0);
        memset(PWD, 0, sizeof PWD);
        {
            static int32_t cat[FN];
            memset(cat, 0, sizeof cat), memcpy(cat, &inl[t0], FN * sizeof(int32_t)), spectrum(cat, 0, PWD);
            memset(cat, 0, sizeof cat), memcpy(cat, &outl[t0 + (uint32_t)d], FN * sizeof(int32_t)), spectrum(cat, 0, PWE);
        }
        cd = centroid(PWD, 8000), ce = centroid(PWE, 8000);
        printf("stereo delay:   a D#2 note (centroid %.0f Hz), TIME %3d ms: its echo's centroid %.0f Hz (%+.0f %%)\n", cd,
               MS[j], ce, 100 * (ce / cd - 1));
        if (MS[j] == 428)
            c428 = ce, cdry = cd;
    }
    snprintf(w, sizeof w, "a note's echo at 428 ms darker than the note (%+.0f %%; the original's gaps -10 %%: 2048 "
             "stages at 428 ms clock at 4.8 kHz, darker than that)", 100 * (c428 / cdry - 1));
    ok(c428 < 0.95 * cdry && c428 > 0.55 * cdry, w);
    per = (uint32_t)(0.428 * FS) + 6u;                  /* (the delay's 428 ms and its filters' few samples) */
    {
        double hit = 0;
        int hitfb = 0;
        for (j = 0; j < NELEM(FB); j++) {
            uint32_t k;
            double e0, e3, g;
            silence();
            for (k = 0; k * per + len < NS; k++)
                note_in(k * per, len, k & 1 ? 116.54 : 77.78, 16000);
            through2(ENGI_MARE, IT_SDLY, knob_of(428), FB[j], 64, 64, NS, ~0u, 0, 0);
            e0 = energy(outl, 0, 2 * per), e3 = energy(outl, 6 * per, 2 * per);
            silence();
            note_in(t0, len, 77.78, 16000);
            through2(ENGI_MARE, IT_SDLY, knob_of(428), FB[j], 64, 127, t0 + 3 * per, ~0u, 0, 0);
            g = sqrt(energy(outl, t0 + 2 * per - 6, len) / energy(outl, t0 + per - 3, len));
            printf("stereo delay:   riff at 428 ms, MIX 64, FDBK %3d: %+.1f dB from the first riff to 2.5 s on; loop gain "
                   "%.2f (%.1f dB a repeat)\n", FB[j], db(e3, e0), g, 20 * log10(g));
            if (fabs(db(e3, e0) - 7) < fabs(hit - 7))
                hit = db(e3, e0), hitfb = FB[j];
        }
        snprintf(w, sizeof w, "riff synced to the delay builds as the original's +7 dB in 3 s at some FDBK (%+.1f dB at "
                 "FDBK %d)", hit, hitfb);
        ok(fabs(hit - 7) < 1.5, w);
    }
}

/* ---- stable: FDBK 127 on a full-scale burst, 5.9 s: it sings on, bounded; FDBK 0 one echo only */
static void t_stable(void)
{
    uint32_t i, g, ty;
    char w[240];
    for (g = 0; g < 2; g++)
        for (ty = IT_SDLY; ty <= IT_PDLY; ty++) {
            int32_t mx = 0;
            double late;
            uint32_t railed = 0;
            silence();
            sd_rng = 7;
            for (i = 4410; i < 4410 + 22050; i++)
                inl[i] = (int32_t)(400000 * white()), inr[i] = (int32_t)(400000 * white());   /* (far past full scale) */
            through2(g ? ENGI_HYBRID : ENGI_MARE, (int)ty, 40, 127, 100, 127, NS, ~0u, 0, 0);
            for (i = 0; i < NS; i++) {
                int32_t a = outl[i] < 0 ? -outl[i] : outl[i], b = outr[i] < 0 ? -outr[i] : outr[i];
                mx = a > mx ? a : mx, mx = b > mx ? b : mx;
                railed += i > 0 && outl[i] == outl[i - 1] && a > 100000;
            }
            late = sqrt(energy(outl, NS - 44100, 44100) / 44100);
            snprintf(w, sizeof w, "%s %s FDBK 127: bounded (peak %d, x %.2f of a voice), still singing after 5 s (rms %.0f)",
                     g ? "other" : "MARE ", ty == IT_SDLY ? "STDLY" : "PPDLY", mx, mx / 24000.0, late);
            ok(mx <= 4 * 32767 + 4 && late > 500 && railed < 1000, w);
        }
    for (g = 0; g < 2; g++)
        for (ty = IT_SDLY; ty <= IT_PDLY; ty++) {       /* a tail dies to exact silence: the part stops (ins_ringing) */
            uint32_t z = 0;
            burst(4410, 4410, 20000);
            through2(g ? ENGI_HYBRID : ENGI_MARE, (int)ty, knob_of(212), 64, 100, 127, NS, ~0u, 0, 0);
            for (i = NS - 44100; i < NS; i++)
                z += outl[i] != 0 || outr[i] != 0;
            snprintf(w, sizeof w, "%s %s FDBK 64: the tail dies to exact 0 (%u samples not 0 in the last second, ring %d)",
                     g ? "other" : "MARE ", ty == IT_SDLY ? "STDLY" : "PPDLY", z, ins[0].ring);
            ok(z == 0 && ins[0].ring <= 3, w);
        }
    {   /* FDBK 0, TIME 780: a quiet burst (-40 dB) keeps ring up until its echo is out (the part runs on for it) */
        int32_t r780;
        burst(4410, 441, 400);
        through2(ENGI_MARE, IT_SDLY, 127, 0, 64, 127, 4410 + (uint32_t)(0.770 * FS), ~0u, 0, 0);
        r780 = ins[0].ring;
        snprintf(w, sizeof w, "FDBK 0, TIME 780, a burst at -40 dB: still ringing just before its echo (ring %d > 3)", r780);
        ok(r780 > 3, w);
    }
    burst(4410, 44, 20000);
    through2(ENGI_MARE, IT_SDLY, knob_of(105), 0, 64, 127, NS / 4, ~0u, 0, 0);
    {
        double e1 = energy(outl, 4410 + 4630 - 1500, 3000), e2 = energy(outl, 4410 + 2 * 4630 - 1500, 3000);
        snprintf(w, sizeof w, "FDBK 0: one echo (%.0f), nothing after it (%.0f)", sqrt(e1 / 3000), sqrt(e2 / 3000));
        ok(e1 > 1e6 && e2 < 1e-4 * e1, w);
    }
}

/* ---- balance: a centred input, left and right within 1 dB over the whole tail */
static void t_balance(void)
{
    static const int SP[] = {0, 32, 64, 96, 127}, FB[] = {0, 64, 100};
    uint32_t ty, j, k, g;
    double worst = 0;
    char w[240], wh[120] = "";
    for (g = 0; g < 2; g++)
        for (ty = IT_SDLY; ty <= IT_PDLY; ty++)
            for (j = 0; j < NELEM(SP); j++)
                for (k = 0; k < NELEM(FB); k++) {
                    double d;
                    burst(4410, 4410, 12000);
                    through2(g ? ENGI_HYBRID : ENGI_MARE, (int)ty, knob_of(212), FB[k], SP[j], 127, NS, ~0u, 0, 0);
                    d = db(energy(outl, 0, NS), energy(outr, 0, NS));
                    if (fabs(d) > fabs(worst))
                        worst = d, snprintf(wh, sizeof wh, "%s %s SPACE %d FDBK %d", g ? "other" : "MARE",
                                            ty == IT_SDLY ? "STDLY" : "PPDLY", SP[j], FB[k]);
                }
    snprintf(w, sizeof w, "centred in: left and right within 1 dB, every type, SPACE, FDBK (worst %+.2f dB: %s)", worst, wh);
    ok(fabs(worst) <= 1.0, w);
    {   /* a left-only burst: STDLY keeps it left; PPDLY's second echo is on the right */
        double d = 212 * FS / 1000, ls, rs, lp, rp;
        uint32_t i;
        burst(4410, 441, 20000);
        for (i = 0; i < NS; i++)
            inr[i] = 0;
        through2(ENGI_MARE, IT_SDLY, knob_of(212), 80, 64, 127, NS / 4, ~0u, 0, 0);
        ls = energy(outl, 4410 + (uint32_t)(1.5 * d), (uint32_t)d), rs = energy(outr, 4410 + (uint32_t)(1.5 * d), (uint32_t)d);
        through2(ENGI_MARE, IT_PDLY, knob_of(212), 80, 64, 127, NS / 4, ~0u, 0, 0);
        lp = energy(outl, 4410 + (uint32_t)(1.5 * d), (uint32_t)d), rp = energy(outr, 4410 + (uint32_t)(1.5 * d), (uint32_t)d);
        snprintf(w, sizeof w, "left-only in, the second echo: STDLY left (R %+.0f dB), PPDLY crossed right (L %+.0f dB)",
                 db(rs, ls), db(lp, rp));
        ok(db(rs, ls) < -30 && db(lp, rp) < -30, w);
    }
}

/* ---- glide: TIME and SPACE turned under a 220 Hz sine, FDBK 0 (the echoes' pitch bends at most x 1.5 while the time
 * shortens: the second difference x 2.25; with feedback each pass bends it again, the spiral of a tape, not measured
 * here; where the clock is held at the rate the read point slides too: x 1.5 on top): no sharp corner in the output (a
 * click), its second difference within 7x the steady one's (a pitch bent at most ~2.6x) */
static double d2max(const int32_t *x, uint32_t from, uint32_t to)
{
    double m = 0;
    uint32_t i;
    for (i = from + 2; i < to; i++) {
        double d = fabs((double)x[i] - 2.0 * x[i - 1] + x[i - 2]);
        m = d > m ? d : m;
    }
    return m;
}
/* the sharpest corner: |second difference| over the largest |first difference| within 24 samples around it (a sine
 * of frequency w: ~w, 0.03 at 220 Hz, 0.08 bent to 570 Hz; a step: ~1) */
static double corner(const int32_t *x, uint32_t from, uint32_t to)
{
    double m = 0;
    uint32_t i, j;
    for (i = from + 26; i < to - 24; i++) {
        double d2 = fabs((double)x[i] - 2.0 * x[i - 1] + x[i - 2]), d1 = 0;
        if (d2 < 8)
            continue;
        for (j = i - 24; j < i + 24; j++)
            d1 = fmax(d1, fabs((double)x[j] - x[j - 1]));
        m = fmax(m, d2 / (d1 + 1));
    }
    return m;
}
static void t_glide(void)
{
    static const struct { uint32_t eng; int ty, fb, a, c, a2, c2; const char *what; } C[] = {
        {ENGI_MARE, IT_SDLY, 0, 0, 64, 127, 64, "MARE TIME 35 -> 780 ms"},
        {ENGI_MARE, IT_SDLY, 0, 127, 64, 0, 64, "MARE TIME 780 -> 35 ms"},
        {ENGI_MARE, IT_PDLY, 0, 0, 64, 12, 64, "MARE TIME 35 -> 47 ms (the short end)"},
        {ENGI_MARE, IT_PDLY, 0, 12, 64, 0, 64, "MARE TIME 47 -> 35 ms (the short end)"},
        {ENGI_MARE, IT_PDLY, 0, 40, 64, 40, 127, "MARE SPACE 0 -> +8"},
        {ENGI_MARE, IT_SDLY, 0, 40, 0, 40, 127, "MARE SPACE -8 -> +8"},
        {ENGI_MARE, IT_SDLY, 0, 4, 0, 4, 127, "MARE SPACE -8 -> +8 at 39 ms (the short end)"},
        {ENGI_HYBRID, IT_SDLY, 0, 0, 64, 127, 64, "other TIME 35 -> 780 ms"},
        {ENGI_HYBRID, IT_PDLY, 0, 60, 127, 60, 0, "other SPACE +8 -> -8"},
    };
    uint32_t j, i, chg = 2u * 44100u / CTL;             /* (at 2 s: the line full, the echoes steady) */
    char w[240];
    double cal;
    for (i = 0; i < NS / 8; i++)                        /* the detector's scale: a 220 Hz sine with a step of 1 % */
        outl[i] = (int32_t)(16000 * sin(2 * M_PI * 220 * i / FS)) + (i >= NS / 16 ? 160 : 0);
    cal = corner(outl, 0, NS / 8);
    snprintf(w, sizeof w, "the click detector sees a step of 1 %% of the sine (sharpest corner %.2f)", cal);
    ok(cal > 0.4, w);
    for (j = 0; j < NELEM(C); j++) {
        double before, after, cn, cb;
        silence();
        for (i = 0; i < NS; i++)
            inl[i] = inr[i] = (int32_t)(16000 * sin(2 * M_PI * 220 * i / FS));
        through2(C[j].eng, C[j].ty, C[j].a, C[j].fb, C[j].c, 64, NS, chg, C[j].a2, C[j].c2);
        before = fmax(d2max(outl, 44100, chg * CTL), d2max(outr, 44100, chg * CTL));
        after = fmax(d2max(outl, chg * CTL, NS), d2max(outr, chg * CTL, NS));
        cn = fmax(corner(outl, chg * CTL, NS), corner(outr, chg * CTL, NS));
        cb = fmax(corner(outl, 44100, chg * CTL), corner(outr, 44100, chg * CTL));
        snprintf(w, sizeof w, "%s: no click (sharpest corner %.3f, %.3f before, a 1 %% step ~0.5; second difference %.0f, %.0f "
                 "before)", C[j].what, cn, cb, after, before);
        ok(cn < 0.25 && after < 7 * before, w);
    }
}

/* ---- MARE's presets through the whole mix (mix_part1: the part's side, DIST, the INSERT, the level), as balance_test
 * renders them: a held note 1.5 s and its tail 2.5 s, left and right energy before the master's limiter.
 *   A  a MARE preset whose dry is mono (left = right sample for sample, found by measuring: the bank changes) stays exactly
 *      centred through STDLY and PPDLY, FDBK 0 and 64, MIX 64 (the PPDLY's two crossed ways alike)
 *   B  every MARE preset: the echo alone (MIX 127, FDBK 0), its whole tail, left and right within 1 dB or no further
 *      apart than the dry's (the wet path level-correct on a stereo voice; the darker echo can only even out an ear
 *      brighter than the other). A held stereo note mixed with its own echo (MIX under 127) combs each
 *      ear's harmonics differently, up to ~4 dB left against right as TIME moves the notches: the comb of any delay on
 *      a stereo sound (the global DLY send does it as much at the same echo level), not a level of this path */
static int mare_mono;                                   /* the last take: left = right, sample for sample */
static void mare_take(uint32_t pi, int ty, int fb, int mix, double *l, double *r)
{
    int32_t o[2 * CTL];
    uint32_t b, i, nb = (44100u * 3u / 2u) / CTL, tail = (44100u * 5u / 2u) / CTL;
    host_tracks_init();
    song.g[G_BPM] = 100;
    memset(trk[0].v, 0, sizeof trk[0].v);
    memset(jc, 0, sizeof jc);
    memset(ins, 0, sizeof ins);
    rng_state = 0x1234567u, uni_seed = 0x1234567u, puni_seed = 0x2468ACEu;
    host_preset(&trk[0], ENGI_MARE, pi);
    trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_CHOR] = 0;
    trk[0].p[P_AMODE] = 0;
    if (ty)
        trk[0].p[P_ITYPE] = (int16_t)ty, trk[0].p[P_IA] = 72, trk[0].p[P_IB] = (int16_t)fb, trk[0].p[P_IC] = 64,
        trk[0].p[P_IMIX] = (int16_t)mix;
    song.sel = 0;
    for (b = 0; b < 44100u / CTL; b++)
        mix_block(o, CTL);
    input_on(&trk[0], 60, 100);
    *l = *r = 0, mare_mono = 1;
    for (b = 0; b < nb + tail; b++) {
        if (b == nb)
            input_off(&trk[0], 60);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            *l += (double)lim_dac.dl[i] * lim_dac.dl[i], *r += (double)lim_dac.dr[i] * lim_dac.dr[i],
            mare_mono &= lim_dac.dl[i] == lim_dac.dr[i];
    }
}
static void t_mare(void)
{
    uint32_t pi, nmono = 0, nst = 0;
    double worstA = 0, worstB = -99;
    char w[260], wa[80] = "", wb[80] = "";
    for (pi = 0; pi < ENGINES[ENGI_MARE]->npresets; pi++) {
        double l0, r0, l, r, d0;
        mare_take(pi, 0, 0, 0, &l0, &r0);
        if (l0 < 1e6)
            continue;                                   /* (silent: nothing to weigh) */
        d0 = db(l0, r0);
        if (mare_mono) {                                /* A: a mono dry */
            static const int T[4][2] = {{IT_SDLY, 0}, {IT_SDLY, 64}, {IT_PDLY, 0}, {IT_PDLY, 64}};
            uint32_t k;
            nmono++;
            for (k = 0; k < 4u; k++) {
                mare_take(pi, T[k][0], T[k][1], 64, &l, &r);
                if (fabs(db(l, r)) > fabs(worstA))
                    worstA = db(l, r), snprintf(wa, sizeof wa, "%s %s FDBK %d", ENGINES[ENGI_MARE]->presets[pi].name,
                                                T[k][0] == IT_SDLY ? "STDLY" : "PPDLY", T[k][1]);
            }
        } else {
            nst++;
        }
        mare_take(pi, IT_SDLY, 0, 127, &l, &r);         /* B: the echo alone, its whole tail: within 1 dB, or no
                                                         * further off than the dry */
        if (fabs(db(l, r)) - fmax(1.0, fabs(d0)) > worstB)
            worstB = fabs(db(l, r)) - fmax(1.0, fabs(d0)), snprintf(wb, sizeof wb, "%s (dry %+.2f, echo %+.2f)",
                                                                  ENGINES[ENGI_MARE]->presets[pi].name, d0, db(l, r));
    }
    snprintf(w, sizeof w, "MARE presets with a mono dry (%u): left = right through STDLY / PPDLY (worst %+.3f dB: %s)",
             nmono, worstA, wa);
    ok(nmono > 0 && fabs(worstA) < 0.05, w);
    snprintf(w, sizeof w, "MARE presets (%u stereo): the echo alone within 1 dB or no further off than the dry (closest: "
             "%+.2f dB past it, %s)", nst, worstB, wb);
    ok(worstB <= 0, w);
}

int main(void)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NPART; i++)
        host_preset(&trk[i], i ? 1u : ENGI_HYBRID, 0);
    t_times();
    t_tone();
    t_riff();
    t_stable();
    t_balance();
    t_glide();
    t_mare();
    if (fails)
        printf("stereo delay: %d FAILED\n", fails);
    return fails != 0;
}
