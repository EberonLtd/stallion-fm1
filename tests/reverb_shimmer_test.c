/* SPDX-License-Identifier: GPL-3.0-only */
/* The reverb's SHIMMER amount (G_RSHIM, REVERB 2 > SHIM; firmware/src/fx.c): the octave-up line on any type
 *   build/host/reverb_shimmer_test
 * 1. at 0, every type as before, bit for bit (ROOM, PLATE, HALL, SHIMR; SIZE 127 DAMP 0; changes of type; SHIMR's tail
 *    up to 20 s without a send, then its octave line off: at rest, and back): the renders' hashes as the firmware before
 *    SHIM gave them (after 20 s, SHIMR's own)
 * 2. SHIMR is HALL with SHIM at 127, bit for bit, past the 20 s too; on SHIMR the knob changes nothing
 * 3. above 0 on PLATE, HALL and ROOM: an octave up in the tail (a 440 Hz burst: the strongest above it at 880 Hz),
 *    more with more SHIM
 * 4. bounded, dying away at SIZE 127 DAMP 0, every amount
 * 5. no flutter: a diffuse send's wet as steady at the shifter's crossing rate (~49 Hz) as without (shimmer_test)
 * 6. the tank still rests after a shimmer tail (PLATE / HALL), not before it is gone; a quiet tail the same as from a
 *    tank that never rests; back at once
 * 7. projects: SHIM kept, also when track 3 has steps (beside the project); a 1.0 project (none stored) loads with it at 0
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <math.h>
#define PROJ_HOST 1
static const uint8_t TRK_DEF[NPART][2] = {{ENGI_HYBRID, 0}, {1, 0}, {4, 5}};   /* (ui.c, Clipper) */
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? TRK_DEF[i][0] : 0u; }
#include "../firmware/src/project.c"

#ifndef RS_SET                                          /* (-D'RS_SET(v)=0': built on the firmware before SHIM, its
                                                         * renders' hashes, BEFORE) */
#define RS_SET(v) (song.g[G_RSHIM] = (int16_t)(v))
#endif
static int bad;
static void check(const char *what, int ok)
{
    printf("reverb_shimmer: %-86s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
#define FXN (FS * 8u)
static int32_t wl[FXN], wr[FXN], inb[FXN];
static uint32_t fnv;
static uint32_t rnd = 12345;
enum { IN_NONE, IN_BURST, IN_NOISE };
/* nblk blocks of the buses, the reverb send only: in (a burst from inb, steady noise, nothing); the wet hashed,
 * kept from at (samples into the run) when rec */
static void fx_run(uint32_t nblk, int in, int rec)
{
    static int32_t z[CTL], r[CTL], ol[CTL], or_[CTL];
    uint32_t b, i;
    for (b = 0; b < nblk; b++) {
        for (i = 0; i < CTL; i++) {
            uint32_t s = b * CTL + i;
            rnd = rnd * 1664525u + 1013904223u;
            r[i] = in == IN_BURST ? (s < FXN ? inb[s] : 0) : in == IN_NOISE ? ((int32_t)(rnd >> 16) - 32768) / 3 : 0;
        }
        fx_buses(z, z, r, ol, or_, CTL);
        for (i = 0; i < CTL; i++) {
            uint32_t s = b * CTL + i;
            fnv = (fnv ^ (uint32_t)ol[i]) * 16777619u;
            fnv = (fnv ^ (uint32_t)or_[i]) * 16777619u;
            if (rec && s < FXN)
                wl[s] = ol[i], wr[s] = or_[i];
        }
    }
}
static void burst(double f, double amp, uint32_t n)
{
    uint32_t i;
    memset(inb, 0, sizeof inb);
    for (i = 0; i < n; i++)
        inb[i] = (int32_t)(amp * sin(2 * M_PI * f * i / FS) * (i < 200u ? i / 200.0 : 1.0));
}
static void set_rev(int ty, int size, int damp, int pre)
{
    song.g[G_RTYPE] = (int16_t)ty, song.g[G_RSIZE] = (int16_t)size, song.g[G_RDAMP] = (int16_t)damp;
    song.g[G_RPRE] = (int16_t)pre;
}
static double erg(const int32_t *x, uint32_t a, uint32_t n)   /* (around the mean: the room's lines rest a few LSB
                                                              * off 0, which the output's DC blocker takes off) */
{
    double e = 0, m = 0;
    uint32_t i;
    for (i = a; i < a + n; i++)
        m += x[i];
    m /= n;
    for (i = a; i < a + n; i++)
        e += (x[i] - m) * (x[i] - m);
    return e / n;
}
static double goertzel(const int32_t *x, uint32_t a, uint32_t n, double f)
{
    double re = 0, im = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(2 * M_PI * i / n);
        re += x[a + i] * w * cos(2 * M_PI * f * i / FS);
        im += x[a + i] * w * sin(2 * M_PI * f * i / FS);
    }
    return sqrt(re * re + im * im) / n;
}
static double band(const int32_t *x, uint32_t a, uint32_t n, double f)   /* the power within 4 % of f (a tail's modes) */
{
    double e = 0, g;
    for (g = f * 0.96; g <= f * 1.04; g += 2.0)
        e += goertzel(x, a, n, g) * goertzel(x, a, n, g);
    return e;
}
/* the wobble (dB, peak to trough) of the power of x[a .. a + n) at f Hz: the power in 1 ms blocks, its DFT at f against
 * its mean (tests/shimmer_test.c) */
static double power_wobble(const int32_t *x, uint32_t a, uint32_t n, double f)
{
    enum { B = 44 };
    uint32_t nb = n / B, i, k;
    double mean = 0, re = 0, im = 0, amp;
    static double pw[FXN / B];
    for (k = 0; k < nb; k++) {
        double e = 0;
        for (i = 0; i < B; i++)
            e += (double)x[a + k * B + i] * x[a + k * B + i];
        pw[k] = e / B;
        mean += pw[k];
    }
    mean /= nb;
    for (k = 0; k < nb; k++) {
        double t = (double)k * B / FS;
        re += (pw[k] - mean) * cos(2 * M_PI * f * t);
        im += (pw[k] - mean) * sin(2 * M_PI * f * t);
    }
    amp = 2.0 * sqrt(re * re + im * im) / nb;
    return 10.0 * log10((mean + amp) / (mean - amp > 1e-9 ? mean - amp : 1e-9));
}

/* 1. the renders at SHIM 0 (and SHIMR): their hashes, as the firmware before SHIM (test-all, 3f5d0a3) gave them. SHIMR's
 * octave line now stops after 20 s without a send (fx.c plate_run: its limit cycles kept the tank from resting): its
 * renders after that point (8, 9) are AFTER's (SIZE 127 DAMP 0, sample by sample against before: 23 LSB apart at most
 * in the silence, 34 in the next tail, -60 dBFS) */
static const uint32_t BEFORE[10] = {0xde2525b7u, 0x5497c2abu, 0x8674061du, 0x0329fefeu, 0x8ed27523u, 0xec377077u, 0x8c9c5511u,
                                    0x22132313u, 0xc77a6776u, 0x6021e690u};
static const uint32_t AFTER[2] = {0x8916db54u, 0x62c89618u};
static void renders(uint32_t *h, int amt)
{
    static const int SW[] = {RT_SHIMMER, RT_ROOM, RT_SHIMMER, RT_PLATE, RT_HALL, RT_ROOM};
    uint32_t ty, k, nb = FS * 4u / CTL;
    RS_SET(amt);
    burst(440.0, 12000.0, FS / 5u);
    for (ty = RT_ROOM; ty <= RT_SHIMMER; ty++) {                /* each type: a burst, its tail */
        set_rev((int)ty, 90, 30, 10);
        fx_run(FS / CTL, IN_NONE, 0);
        fnv = 2166136261u;
        fx_run(nb, IN_BURST, 0);
        h[ty] = fnv;
    }
    for (ty = RT_HALL; ty <= RT_SHIMMER; ty++) {                /* the longest */
        set_rev((int)ty, 127, 0, 0);
        fx_run(FS / CTL, IN_NONE, 0);
        fnv = 2166136261u;
        fx_run(nb, IN_BURST, 0);
        h[4u + ty - RT_HALL] = fnv;
    }
    fnv = 2166136261u;                                          /* changes of type on a steady send */
    set_rev(RT_PLATE, 100, 20, 20);
    for (k = 0; k < NELEM(SW); k++) {
        song.g[G_RTYPE] = (int16_t)SW[k];
        fx_run(FS / 2u / CTL, IN_NOISE, 0);
    }
    h[6] = fnv;
    fnv = 2166136261u;                                          /* SHIMR's tail: its first 19 s, */
    set_rev(RT_SHIMMER, 110, 10, 30);
    fx_run(nb, IN_BURST, 0);
    fx_run(FS * 15u / CTL, IN_NONE, 0);
    h[7] = fnv;
    fnv = 2166136261u;                                          /* then 20 s without a send: at rest, */
    fx_run(FS * 10u / CTL, IN_NONE, 0);
    h[8] = fnv;
    fnv = 2166136261u;                                          /* and back */
    fx_run(nb, IN_BURST, 0);
    h[9] = fnv;
}

int main(void)
{
    char w[220];
    uint32_t h[10], k;
    static const char *const NM[4] = {"ROOM", "PLATE", "HALL", "SHIMR"};
    host_tracks_init();
    song.g[G_DMIX] = 0, song.g[G_DFDBK] = 0, song.g[G_CDEPTH] = 0;

    renders(h, 0);                                              /* 1. */
    for (k = 0; k < 10u; k++)
        printf("reverb_shimmer:   render %u: %08x (before %08x)\n", (unsigned)k, (unsigned)h[k], (unsigned)BEFORE[k]);
    check("SHIM 0: ROOM PLATE HALL SHIMR, SIZE 127 DAMP 0, changes of type, SHIMR's first 19 s: as before, bit for bit",
          !memcmp(h, BEFORE, 8u * sizeof h[0]));
    snprintf(w, sizeof w, "... then 20 s without a send: SHIMR's octave line off, at rest, back (renders %08x %08x as AFTER)",
             (unsigned)h[8], (unsigned)h[9]);
    check(w, h[8] == AFTER[0] && h[9] == AFTER[1]);

    {   /* 2. SHIMR = HALL + SHIM 127; the knob on SHIMR: nothing (each from a cleared tank) */
        static const int TY[3] = {RT_HALL, RT_SHIMMER, RT_SHIMMER}, AM[3] = {127, 0, 64};
        uint32_t hs[3];
        burst(440.0, 12000.0, FS / 5u);
        for (k = 0; k < 3u; k++) {
            set_rev(TY[k], 100, 20, 10), RS_SET(AM[k]);
            fx_run(1, IN_NONE, 0);
            memset(&pl, 0, sizeof pl), pl_quiet = pl_silent = 0;
            memset(ps_buf, 0, sizeof ps_buf), memset(&ps, 0, sizeof ps);
            fnv = 2166136261u, fx_run(FS * 4u / CTL, IN_BURST, 0), fx_run(FS * 26u / CTL, IN_NONE, 0);
            fx_run(FS / CTL, IN_BURST, 0), hs[k] = fnv;
        }
        check("SHIMR = HALL with SHIM 127, bit for bit (past 20 s without a send, at rest, back); SHIM on SHIMR changes nothing",
              hs[0] == hs[1] && hs[1] == hs[2]);
    }

    {   /* 3. an octave up in the tail */
        static const int TY[3] = {RT_ROOM, RT_PLATE, RT_HALL}, AM[3] = {0, 64, 127};
        int good = 1;
        burst(440.0, 12000.0, FS / 5u);
        for (k = 0; k < 3u; k++) {
            double oct[3], best = 0, fb = 0, f;
            uint32_t j, at = TY[k] == RT_ROOM ? FS / 2u : FS * 3u / 2u;   /* (a second of the tail; ROOM's shorter) */
            for (j = 0; j < 3u; j++) {
                set_rev(TY[k], 90, 30, 10), RS_SET(AM[j]);
                fx_run(FS * 2u / CTL, IN_NONE, 0);
                fx_run(FXN / CTL, IN_BURST, 1);
                oct[j] = sqrt(band(wl, at, FS, 880.0) / (band(wl, at, FS, 440.0) + 1e-9));
            }
            for (f = 500.0; f <= 2000.0; f += 5.0) {            /* the strongest above the note (SHIM 127) */
                double m = goertzel(wl, at + FS / 2u - 4096u, 8192, f);
                if (m > best)
                    best = m, fb = f;
            }
            printf("reverb_shimmer:   %-5s %.1f .. %.1f s, 880 / 440 Hz: SHIM 0 %.3f, 64 %.3f, 127 %.3f; above 500 Hz the most at %.0f Hz\n",
                   NM[TY[k]], (double)at / FS, (double)at / FS + 1.0, oct[0], oct[1], oct[2], fb);
            good &= oct[2] > 3.0 * oct[0] && oct[2] > 0.1 && oct[1] > oct[0] && oct[2] > oct[1] && fb >= 860 && fb <= 900;
        }
        check("SHIM on ROOM PLATE HALL: an octave up in the tail (880 Hz the strongest above it), more with more", good);
    }

    {   /* 4. bounded, dying away: SIZE 127, DAMP 0 */
        static const int TY[3] = {RT_ROOM, RT_PLATE, RT_HALL}, AM[4] = {32, 64, 96, 127};
        int good = 1;
        double worst = -999;
        burst(440.0, 16000.0, FS / 2u);
        for (k = 0; k < 3u; k++) {
            uint32_t j, i;
            for (j = 0; j < 4u; j++) {
                double e2, e7, d;
                set_rev(TY[k], 127, 0, 0), RS_SET(AM[j]);
                fx_run(FS * 2u / CTL, IN_NONE, 0);
                fx_run(FXN / CTL, IN_BURST, 1);
                for (i = 0; i < FXN; i++)
                    good &= wl[i] < (1 << 23) && wl[i] > -(1 << 23) && wr[i] < (1 << 23) && wr[i] > -(1 << 23);
                e2 = erg(wl, FS * 2u, FS), e7 = erg(wl, FS * 7u, FS - CTL);
                d = 10 * log10((e7 + 1) / (e2 + 1));
                worst = d > worst ? d : worst;
                good &= e7 < 0.1 * e2;
                if (AM[j] == 127)
                    printf("reverb_shimmer:   %-5s SIZE 127 DAMP 0 SHIM 127: 7 s vs 2 s %6.1f dB\n", NM[TY[k]], d);
            }
        }
        snprintf(w, sizeof w, "SHIM 32..127 on ROOM PLATE HALL at SIZE 127 DAMP 0: bounded, dying away (worst 7 s vs 2 s %.1f dB)",
                 worst);
        check(w, good);
    }

    {   /* 5. no flutter at the shifter's crossing rate */
        static const int TY[3] = {RT_ROOM, RT_PLATE, RT_HALL};
        double fc = 22050.0 / (PS_WIN / 2u), wmax = 0;
        for (k = 0; k < 3u; k++) {
            double w0, w1;
            set_rev(TY[k], 90, 30, 10), RS_SET(0);
            fx_run(FS * 2u / CTL, IN_NOISE, 0), fx_run(FXN / CTL, IN_NOISE, 1);
            w0 = power_wobble(wl, FS, FXN - FS, fc);
            RS_SET(127);
            fx_run(FS * 2u / CTL, IN_NOISE, 0), fx_run(FXN / CTL, IN_NOISE, 1);
            w1 = power_wobble(wl, FS, FXN - FS, fc);
            printf("reverb_shimmer:   %-5s noise sent: the wet's wobble at %.1f Hz, SHIM 0 %.2f dB, 127 %.2f dB\n", NM[TY[k]], fc,
                   w0, w1);
            wmax = w1 > wmax ? w1 : wmax;
        }
        snprintf(w, sizeof w, "SHIM 127 on ROOM PLATE HALL: no flutter (the wet's wobble at the crossing rate at most %.2f dB, < 1)",
                 wmax);
        check(w, wmax < 1.0);
    }

    {   /* 6. the tank at rest after a shimmer tail */
        int32_t pk, p2, ms, first = -1, i;
        uint32_t b;
        set_rev(RT_HALL, 127, 0, 100), RS_SET(127);
        burst(146.83, 8000.0, FS);
        fx_run(FS * 2u / CTL, IN_NONE, 0);
        fx_run(FS * 3u / CTL, IN_BURST, 1);
        for (pk = 0, i = 0; i < (int32_t)FS; i++)
            pk = abs(wl[i]) > pk ? abs(wl[i]) : pk;
        for (p2 = 0, i = FS * 2; i < (int32_t)(FS * 3u / CTL * CTL); i++)
            p2 = abs(wl[i]) > p2 ? abs(wl[i]) : p2;
        snprintf(w, sizeof w, "HALL + SHIM 127 at its longest: 2 s after the send the tail still rings (peak %d of %d), not at rest",
                 (int)p2, (int)pk);
        check(w, p2 > 1000 && pl_quiet < PL_QUIET);
        for (ms = 3000; ms < 60000 && pl_quiet < PL_QUIET; ms += 100) {
            fx_run(FS / 10u / CTL, IN_NONE, 1);
            for (p2 = 0, i = 0; i < (int32_t)(FS / 10u / CTL * CTL); i++)
                p2 = abs(wl[i]) > p2 ? abs(wl[i]) : p2;
        }
        snprintf(w, sizeof w, "then at rest %.1f s after the send stopped, its last 100 ms at the last bits (peak %d, <= %d)",
                 ms / 1000.0 - 1.0, (int)p2, PL_LONG_LVL);
        check(w, pl_quiet >= PL_QUIET && p2 <= PL_LONG_LVL && ms < 31000);
        fx_run(FS / CTL, IN_BURST, 1);
        for (i = 0; i < (int32_t)(FS / CTL * CTL) && first < 0; i++)
            if (wl[i])
                first = (int32_t)(i * 1000 / (int32_t)FS);
        snprintf(w, sizeof w, "sent to again: the wet back after %d ms (< 200)", (int)first);
        check(w, first >= 0 && first < 200);

        {   /* SHIMR at SIZE 110 and up (its limit cycles never let it rest before the 20 s cut-off) */
            static const int SZ[2] = {110, 127}, DP[2] = {10, 0};
            double worst = 0;
            int good = 1;
            for (k = 0; k < 2u; k++) {
                set_rev(RT_SHIMMER, SZ[k], DP[k], 10), RS_SET(0);
                burst(146.83, 8000.0, FS);
                fx_run(FS * 3u / CTL, IN_BURST, 0);
                for (ms = 2000; ms < 60000 && pl_quiet < PL_QUIET; ms += 100)
                    fx_run(FS / 10u / CTL, IN_NONE, 0);
                good &= pl_quiet >= PL_QUIET && ms <= 25000;
                worst = ms / 1000.0 > worst ? ms / 1000.0 : worst;
            }
            snprintf(w, sizeof w, "SHIMR at SIZE 110 DAMP 10, SIZE 127 DAMP 0: at rest by %.1f s after the send (<= 24)", worst);
            check(w, good);
        }
        {   /* a quiet note's tail on HALL + SHIM 64: at rest, against a tank that never rests (the first 20 s within
             * the last bits; after them, 20 s without a send, PL_LONG_LVL's limit cycles may be cut) */
            static int32_t tl[2][FS * 23u];
            int32_t worst = 0, top = 0, late = 0;
            uint32_t n = FS * 23u / CTL * CTL, rest_at = 0, j;
            set_rev(RT_HALL, 90, 30, 10), RS_SET(64);
            burst(146.83, 160.0, FS);
            for (j = 0; j < 2u; j++) {
                memset(&pl, 0, sizeof pl), pl_quiet = pl_silent = 0;
                memset(ps_buf, 0, sizeof ps_buf), memset(&ps, 0, sizeof ps);
                for (b = 0; b < n / CTL; b++) {
                    int32_t in[CTL], l[CTL], r[CTL];
                    for (i = 0; i < (int32_t)CTL; i++)
                        in[i] = b * CTL + i < FS ? inb[b * CTL + i] : 0, l[i] = r[i] = 0;
                    if (j)
                        pl_quiet = 0;
                    plate_run(in, l, r, CTL, 32767, 32767);
                    for (i = 0; i < (int32_t)CTL; i++)
                        tl[j][b * CTL + i] = l[i];
                }
                if (!j)
                    rest_at = pl_quiet >= PL_QUIET;
            }
            for (j = 0; j < n; j++) {
                int32_t d = abs(tl[0][j] - tl[1][j]);
                if (j < FS * 21u)
                    worst = d > worst ? d : worst;
                else
                    late = d > late ? d : late;
                top = abs(tl[1][j]) > top ? abs(tl[1][j]) : top;
            }
            snprintf(w, sizeof w, "a quiet tail, HALL + SHIM 64 (peak %d): at rest by 23 s; against never at rest %d apart (<= %d), then %d (<= %d)",
                     (int)top, (int)worst, PL_QUIET_LVL + 1, (int)late, PL_LONG_LVL);
            check(w, rest_at && worst <= PL_QUIET_LVL + 1 && late <= PL_LONG_LVL);
        }
    }
    {   /* 7. projects */
        static project_t pj;
        uint32_t i;
        int kept, old, steps;
        for (i = 0; i < NSTEP; i++)
            trk[2].step[i].n = 0;
        song.g[G_RSHIM] = 99, song.g[G_DMODE] = DM_TAPE;
        proj_capture(&pj);
        song.g[G_RSHIM] = 5, song.g[G_DMODE] = DM_MONO;
        proj_apply(&pj, 1);
        kept = song.g[G_RSHIM] == 99 && song.g[G_DMODE] == DM_TAPE;
        pj.t[2].micro[PROJ_X10_SH] = pj.t[2].micro[PROJ_X10_SH + 1u] = 0;   /* (as 1.0 wrote it) */
        memset(&proj_xss[proj_x10_at(&pj)], 0, sizeof proj_xss[0]);   /* (nor the block beside it) */
        song.g[G_RSHIM] = 5;
        proj_apply(&pj, 1);
        old = song.g[G_RSHIM] == 0 && song.g[G_DMODE] == DM_TAPE;
        trk[2].step[3].n = 1, trk[2].step[3].note[0] = 60;
        song.g[G_RSHIM] = 99;
        proj_capture(&pj);
        song.g[G_RSHIM] = 5;
        proj_apply(&pj, 1);
        steps = song.g[G_RSHIM] == 99;                  /* (Stallion since 1.3: beside the project, stallion R38) */
        trk[2].step[3].n = 0;
        check("project: SHIM kept, with steps on track 3 too (beside the project); a 1.0 project loads with SHIM 0 (MODE as before)",
              kept && old && steps && sizeof pj == 3840u);
    }
    {   /* the page: FX > REVERB 2, SHIM */
        uint32_t i, on = 0;
        for (i = 0; i < NPAGES; i++)
            on |= !strcmp(PAGES[i].title, "REVERB 2") && PAGES[i].scope == SC_GLOBAL && PAGES[i].id[0] == G_RSHIM &&
                  !strcmp(PAGES[i - 1u].title, "REVERB");
        check("FX > REVERB 2 (after REVERB): SHIM, 0..127, 0 by default",
              on && !strcmp(GP[G_RSHIM].label, "SHIM") && GP[G_RSHIM].min == 0 && GP[G_RSHIM].max == 127 && GP[G_RSHIM].def == 0);
    }
    printf(bad ? "reverb_shimmer: %d FAILED\n" : "reverb_shimmer: all passed\n", bad);
    return bad;
}
